/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <libxml/parser.h>
#include "comm_headers.h"
#include "cnxman-socket.h"
#include "log.h"
#include "debug.h"

#define CLUSTER_PORT_CCSD 50
#define CLUSTER_PORT_CCSD_RESPONSE 51

int quorate = 0;
int update_config = 0;

static int cman_sock = -1;

static void cman_sig_handler(int sig){
  int error = 0;
  ENTER("cman_sig_handler");
  if((error = ioctl(cman_sock, SIOCCLUSTER_ISQUORATE, 0)) < 0){
    log_err("Ioctl to cman socket failed: %s\n", strerror(-error));
    log_err("Exiting...\n");
    exit(EXIT_FAILURE);
  } else {
    quorate = error;
  }
  log_dbg("Cluster %s quorate.\n", (quorate)? "is": "is not");
  EXIT("cman_sig_handler");  
}

static void cman_communicator(void){
  int error = 0;
  int len = 0;
  int arg = SIGUSR1;
  int update_progress = 0;
  char *mem_doc=NULL;
  int fd;
  comm_header_t ch;
  struct msghdr msg;
  struct iovec iov[2];
  struct sockaddr_cl saddr;

  ENTER("cman_communicator");

  signal(SIGUSR1, &cman_sig_handler);

  while(cman_sock < 0){
    cman_sock = socket(AF_CLUSTER, SOCK_DGRAM, CLPROTO_CLIENT);
  }
  log_dbg("Connection to cman established.\n");

  saddr.scl_family = AF_CLUSTER;
  saddr.scl_port = CLUSTER_PORT_CCSD;

  if(bind(cman_sock, (struct sockaddr *)&saddr, sizeof(struct sockaddr_cl))){
    log_sys_err("Unable to bind cluster socket");
    log_err("Exiting...\n");
    exit(EXIT_FAILURE);
  }
  /* ATTENTION -- doen't need 'error' variable, cleanup */

  if((error = ioctl(cman_sock, SIOCCLUSTER_NOTIFY, arg))){
    log_err("Ioctl to cman socket failed: %s\n", strerror(-error));
    log_err("Exiting...\n");
    exit(EXIT_FAILURE);
  }
  if((error = ioctl(cman_sock, SIOCCLUSTER_ISQUORATE, 0)) < 0){
    log_err("Ioctl to cman socket failed: %s\n", strerror(-error));
    log_err("Exiting...\n");
    exit(EXIT_FAILURE);
  } else {
    quorate = error;
    error = 0;
  }
  while(1){ /* Wait for signals and update notices */
    memset(&ch, 0, sizeof(comm_header_t));
    memset(&msg, 0, sizeof(struct msghdr));
    msg.msg_iovlen = 1;
    msg.msg_iov = iov;
    msg.msg_name = &saddr;
    msg.msg_namelen = sizeof(struct sockaddr_cl);

    iov[0].iov_len = sizeof(comm_header_t);
    iov[0].iov_base= &ch;

    log_dbg("Waiting to receive cluster message.\n");
    len = recvmsg(cman_sock, &msg, MSG_PEEK);
    if(len < 0){
      log_sys_err("Unable to receive cluster message");
      continue;
    }
    log_dbg("Received msg on cluster socket.\n");
    if(ch.comm_type != COMM_UPDATE){
      recvmsg(cman_sock, &msg, 0);  /* clear out msg */
      log_err("Received bad communication type on cluster socket.\n");
      log_dbg("Msg looks like:\n");
      log_dbg("%s\n", (char *)&ch);
      continue;
    }

    if((ch.comm_flags & COMM_UPDATE_NOTICE) && !update_progress){
      log_msg("Update notice received.\n");
      if(!quorate){
	/* ATTENTION -- complain */
	log_err("Wow!  Update attempted on inquorate cluster.\n");
	log_err("Need to notify sender of failure...\n");
	continue;
      }

      log_dbg(" Doc size = %d\n", ch.comm_payload_size);
      mem_doc = malloc(ch.comm_payload_size);
      if(!mem_doc){
	log_err("Insufficient memory to receive update payload.\n");
	/* ATTENTION -- notify sender of failure */
	continue;
      }
      memset(mem_doc, 0, ch.comm_payload_size);
      msg.msg_iovlen = 2;
      iov[1].iov_len = ch.comm_payload_size;
      iov[1].iov_base= mem_doc;
      recvmsg(cman_sock, &msg, 0);
      fd = open("/etc/cluster/cluster.xml-update",
		O_CREAT | O_WRONLY | O_TRUNC,
		S_IRUSR | S_IRGRP);
      if(fd < 0){
	log_sys_err("Unable to open /etc/cluster/cluster.xml-update");
	free(mem_doc);
	/* ATTENTION -- send failure notice */
	continue;
      }
      if(write(fd, mem_doc, ch.comm_payload_size) < 0){
	log_sys_err("Unable to write /etc/cluster/cluster.xml-update");
	free(mem_doc);
	/* ATTENTION -- send failure notice */
	continue;
      }
      free(mem_doc);
      log_msg("Upload of file complete.\n");
      ch.comm_flags = COMM_UPDATE_NOTICE_ACK;
      ch.comm_payload_size = 0;
      msg.msg_iovlen = 1;
      log_dbg("Sending COMM_UPDATE_NOTICE_ACK.\n");

      saddr.scl_port = CLUSTER_PORT_CCSD_RESPONSE;
      if((error = sendmsg(cman_sock, &msg, 0)) < 0){
	log_sys_err("Unable to send message on cluster response channel.\n");
      } else {
	log_dbg("Sendmsg (CCSD_RESPONSE) returns %d\n", error);
      }
      update_progress = 1;
    } else if((ch.comm_flags & COMM_UPDATE_COMMIT) && (update_progress == 1)){
      xmlDocPtr tmp_doc = NULL;
      recvmsg(cman_sock, &msg, 0);  /* clear out message */
      tmp_doc = xmlParseFile("/etc/cluster/cluster.xml-update");
      if(!tmp_doc){
	log_err("Unable to parse updated config file.\n");
	/* ATTENTION -- send error back to update master */
	update_progress = 0;
	continue;
      }    
      xmlFreeDoc(tmp_doc);
      rename("/etc/cluster/cluster.xml-update", "/etc/cluster/cluster.xml");
      ch.comm_flags = COMM_UPDATE_COMMIT_ACK;
      ch.comm_payload_size = 0;
      msg.msg_iovlen = 1;
      log_dbg("Sending COMM_UPDATE_COMMIT_ACK.\n");

      saddr.scl_port = CLUSTER_PORT_CCSD_RESPONSE;
      sendmsg(cman_sock, &msg, 0);
      update_config=1;
      update_progress=0;
    } else {	      
      recvmsg(cman_sock, &msg, 0);  /* clear out message */
      log_err("Bad update exchange.\n");
      log_err(" ch.comm_flags = %s\n",
	      (ch.comm_flags & COMM_UPDATE_NOTICE)?
	      "COMM_UPDATE_NOTICE":
	      (ch.comm_flags & COMM_UPDATE_NOTICE_ACK)?
	      "COMM_UPDATE_NOTICE_ACK":
	      (ch.comm_flags & COMM_UPDATE_COMMIT)?
	      "COMM_UPDATE_COMMIT":
	      (ch.comm_flags & COMM_UPDATE_COMMIT_ACK)?
	      "COMM_UPDATE_COMMIT_ACK":
	      "UNKNOWN");
      log_err(" update_progress = %d\n", update_progress);
      update_progress = 0;
      ch.comm_flags = 0;
      ch.comm_error = -EBADE;
      ch.comm_payload_size = 0;
      msg.msg_iovlen = 1;
      log_dbg("Sending COMM_UPDATE error.\n");
      saddr.scl_port = CLUSTER_PORT_CCSD_RESPONSE;
      sendmsg(cman_sock, &msg, 0);
    }
  }

  EXIT("cman_communicator");
}

int start_cman_monitor_thread(void){
  int error = 0;
  pthread_t	thread;
  
  ENTER("start_cman_monitor_thread");

  error = pthread_create(&thread, NULL, (void *)cman_communicator, NULL);
  if(error){
    log_err("Failed to create thread: %s\n", strerror(-error));
    goto fail;
  }

 fail:
  EXIT("start_cman_monitor_thread");
  return error;
}


int update_remote_nodes(char *mem_doc, int doc_size){
  int error=0;
  int cman_resp = -1;
  int member_count, mc;
  fd_set rset;
  struct timeval tv;
  comm_header_t ch;
  struct msghdr msg;
  struct iovec iov[2];
  struct sockaddr_cl saddr;

  ENTER("update_remote_nodes");

  cman_resp = socket(AF_CLUSTER, SOCK_DGRAM, CLPROTO_CLIENT);
  if(cman_resp < 0){
    log_err("Unable to create cluster socket.\n");
    error = cman_resp;
    goto fail;
  }

  saddr.scl_family = AF_CLUSTER;
  saddr.scl_port = CLUSTER_PORT_CCSD_RESPONSE;

  if(bind(cman_resp, (struct sockaddr *)&saddr, sizeof(struct sockaddr_cl))){
    log_sys_err("Unable to bind cluster socket");
    error = -errno;
    goto fail;
  }
  
  memset(&ch, 0, sizeof(comm_header_t));
  ch.comm_type = COMM_UPDATE;
  ch.comm_flags= COMM_UPDATE_NOTICE;
  ch.comm_payload_size = doc_size;

  memset(&msg, 0, sizeof(struct msghdr));
  msg.msg_iov = iov;
  msg.msg_iovlen = 2;

  iov[0].iov_len = sizeof(comm_header_t);
  iov[0].iov_base= &ch;
  iov[1].iov_len = doc_size;
  iov[1].iov_base= mem_doc;

  log_dbg("sizeof(comm_header_t) = %d\n", sizeof(comm_header_t));
  log_dbg("doc_size = %d\n", doc_size);

  mc = ioctl(cman_resp, SIOCCLUSTER_GETMEMBERS, NULL);
  if(mc < 0){
    log_sys_err("Ioctl failed");
    error = mc;
    goto fail;
  }
  mc--;  /* do not include ourself */
  member_count = mc;
  log_dbg("Expecting %d responses.\n", member_count);

  error = sendmsg(cman_sock, &msg, 0);
  log_dbg("sendmsg returns: %d\n", error);
  msg.msg_iovlen = 1;
  msg.msg_name = &saddr;
  msg.msg_namelen = sizeof(struct sockaddr_cl);

  while(1){
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    FD_ZERO(&rset);
    FD_SET(cman_resp, &rset);
    log_dbg("Waiting 1 seconds for UPDATE_NOTICE_ACK...\n");
    error = select(FD_SETSIZE, &rset, NULL, NULL, &tv);
    if(error < 0){
      log_sys_err("Select on cluster socket failed");
      error = -errno;
      goto fail;
    } else if(error){
      recvmsg(cman_resp, &msg, MSG_OOB);
      if(ch.comm_type != COMM_UPDATE){
	log_err("Bad response - ignoring.\n");
	continue;
      }
      if(!(ch.comm_flags & COMM_UPDATE_NOTICE_ACK)){
	log_err("Expected COMM_UPDATE_NOTICE_ACK, but got 0x%x\n", ch.comm_flags);
	error = -EBADE;
	goto fail;
      }
      if(ch.comm_error){
	/* ATTENTION -- should clear out other responses and abort update */
	log_err("Remote node reports error during update.\n");
	error = ch.comm_error;
	goto fail;
      }
      log_dbg("UPDATE_NOTICE_ACK received.\n");
      member_count--;
      error = 0;
    } else {
      log_dbg("Select reports no more messages.\n");
      break;
    }
  }

  if(member_count){
    log_err("Incorrect number of responses (%d) from update notice.\n", mc-member_count);
    error = -EBADE;
    goto fail;
  }
  member_count = mc;

  msg.msg_name = NULL;
  msg.msg_namelen = 0;
  msg.msg_iovlen = 1;
  ch.comm_flags = COMM_UPDATE_COMMIT;

  error = sendmsg(cman_sock, &msg, 0);
  if(error < 0){
    log_sys_err("Failed to send message on cluster socket");
    error = -errno;
    goto fail;
  }
  log_dbg("sendmsg returns: %d\n", error);
  msg.msg_name = &saddr;
  msg.msg_namelen = sizeof(struct sockaddr_cl);
  while(1){
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    FD_ZERO(&rset);
    FD_SET(cman_resp, &rset);
    log_dbg("Waiting 1 second for UPDATE_COMMIT_ACK...\n");
    error = select(FD_SETSIZE, &rset, NULL, NULL, &tv);
    if(error < 0){
      log_sys_err("Select on cluster socket failed");
      error = -errno;
      goto fail;
    } else if(error){
      recvmsg(cman_resp, &msg, MSG_OOB);
      if(ch.comm_type != COMM_UPDATE){
	log_err("Bad response - ignoring.\n");
	continue;
      }
      if(!(ch.comm_flags & COMM_UPDATE_COMMIT_ACK)){
	log_err("Expected COMM_UPDATE_COMMIT_ACK, but got 0x%x\n", ch.comm_flags);
	error = -EBADE;
	goto fail;
      }
      if(ch.comm_error){
	/* ATTENTION -- should clear out other responses and abort update */
	log_err("Remote node reports error during update.\n");
	error = ch.comm_error;
	goto fail;
      }
      log_dbg("UPDATE_NOTICE_ACK received.\n");
      member_count--;
      error = 0;
    } else {
      break;
    }
  }

  if(member_count){
    log_err("Incorrect number of responses (%d) from update notice.\n", mc-member_count);
    error = -EBADE;
    goto fail;
  }

 fail:	
  if(cman_resp >= 0) {
    close(cman_resp);
  }
  EXIT("update_remote_nodes");
  return error;
}
