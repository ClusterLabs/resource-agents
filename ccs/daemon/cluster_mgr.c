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
#include "log.h"
#include "debug.h"

#include "magma.h"
#include "magmamsg.h"

#define UPDATE_BASE_PORT 50008

int quorate = 0;
int update_required = 0;

cluster_member_list_t *membership = NULL;

static void cluster_communicator(void){
  int cluster_fd = -1;
  int listen_fds[2], listeners;
  int fd;
  int afd;
  int error;
  uint64_t nodeid;
  fd_set rset;
  int max_fds, n;
  comm_header_t ch;

  ENTER("cluster_communicator");

  if ((listeners = msg_listen(UPDATE_BASE_PORT, 0, listen_fds, 2)) <= 0) {
    log_err("Unable to setup update listener socket.\n");
    exit(EXIT_FAILURE);
  }

 restart:
  while(cluster_fd < 0){
    cluster_fd = clu_connect(NULL, 0);
  }

  log_msg("Connected to cluster infrastruture via: %s\n", clu_plugin_version());

  quorate = (clu_quorum_status(NULL) & QF_QUORATE);
  log_msg("Initial status:: %s\n", (quorate)? "Quorate" : "Inquorate");

  membership = clu_member_list(NULL);
  msg_update(membership);
  memb_resolve_list(membership, NULL);

  while(1) {
    max_fds = msg_fill_fdset(&rset, MSG_ALL, MSGP_ALL);
    log_dbg("Waiting for cluster event.\n");
    n = select(max_fds+1, &rset, NULL, NULL, NULL);
    
    if(n < 0){
      log_sys_err("Select failed");
      continue;
    }
    log_dbg("There are %d cluster messages waiting.\n", n);

    while(n){
      fd = msg_next_fd(&rset);
      if(fd == -1) { break; }
      n--;
      if(fd == cluster_fd){
	switch(clu_get_event(cluster_fd)) {
	case CE_NULL:
	  log_dbg("-E- Spurious wakeup\n");
	  break;
	case CE_SUSPEND:
	  log_dbg("*E* Suspend activities\n");
	  break;
	case CE_MEMB_CHANGE:
	  log_dbg("*E* Membership change\n");
	  cml_free(membership);
	  membership = clu_member_list(NULL);
	  memb_resolve_list(membership, NULL);
	  msg_update(membership);
	  break;
	case CE_QUORATE:
	  log_dbg("*E* Quorum formed\n");
	  cml_free(membership);
	  membership = clu_member_list(NULL);
	  memb_resolve_list(membership, NULL);
	  msg_update(membership);
	  quorate = 1;
	  break;
	case CE_INQUORATE:
	  log_dbg("*E* Quorum dissolved\n");
	  cml_free(membership);
	  membership = clu_member_list(NULL);
	  memb_resolve_list(membership, NULL);
	  msg_update(membership);
	  quorate = 0;
	  break;
	case CE_SHUTDOWN:
	  log_dbg("*E* Node shutdown\n");
	  quorate = 0;
	  clu_disconnect(cluster_fd);
	  cluster_fd = -1;
	  goto restart;
	default:
	  log_dbg("-E- Unknown cluster event\n");
	}
      } else {
	char *buffer;
	int file_fd;
	xmlDocPtr tmp_doc = NULL;

	afd = msg_accept(fd, 1, &nodeid);
	error = msg_peek(afd, &ch, sizeof(comm_header_t));
	if(error < 0){
	  log_sys_err("Failed to receive message from %s\n",
		      memb_id_to_name(membership,nodeid));
	  msg_close(afd);
	  continue;
	}
	log_dbg("Message (%d bytes) received from %s\n", error,
		memb_id_to_name(membership,nodeid));
	if(ch.comm_type != COMM_UPDATE){
	  log_err("Unexpected communication type... ignoring.\n");
	  msg_close(afd);
	  continue;
	}

	if(ch.comm_flags == COMM_UPDATE_NOTICE){

	  buffer = malloc(ch.comm_payload_size + sizeof(comm_header_t));
	  if(!buffer){
	    log_err("Unable to allocate space to perform update.\n");
	    msg_close(afd);
	    continue;
	  }
	
	  log_dbg("Updated config size:: %d\n", ch.comm_payload_size);

	  error = msg_receive_timeout(afd, buffer, 
				      ch.comm_payload_size+sizeof(comm_header_t), 5);

	  if(error < 0){
	    log_sys_err("Unable to retrieve updated config");
	    free(buffer);
	    msg_close(afd);
	    continue;
	  }

	  file_fd = open("/etc/cluster/cluster.conf-update",
			 O_CREAT | O_WRONLY | O_TRUNC,
			 S_IRUSR | S_IRGRP);
	  if(file_fd < 0){
	    log_sys_err("Unable to open /etc/cluster/cluster.conf-update");
	    free(buffer);
	    msg_close(afd);
	    continue;
	  }

	  if(write(file_fd, buffer+sizeof(comm_header_t), ch.comm_payload_size) < 0){
	    log_sys_err("Unable to write /etc/cluster/cluster.conf-update");
	    free(buffer);
	    close(file_fd);
	    msg_close(afd);
	    continue;
	  }

	  close(file_fd);
	  free(buffer);
	  log_dbg("Upload of new config file complete.\n");

	  tmp_doc = xmlParseFile("/etc/cluster/cluster.conf-update");
	  if(!tmp_doc){
	    log_err("Unable to parse updated config file.\n");
	    /* ATTENTION -- send error back to update master */
	    msg_close(afd);
	    continue;
	  }
	  xmlFreeDoc(tmp_doc);

	  ch.comm_payload_size = 0;
	  ch.comm_flags = COMM_UPDATE_NOTICE_ACK;
	  log_dbg("Sending COMM_UPDATE_NOTICE_ACK.\n");
	  msg_send(afd, &ch, sizeof(comm_header_t));
	  msg_close(afd);
	} else if(ch.comm_flags == COMM_UPDATE_COMMIT){
	  error = msg_receive_timeout(afd, &ch, sizeof(comm_header_t), 5);
	  if((ch.comm_type != COMM_UPDATE) || (ch.comm_flags != COMM_UPDATE_COMMIT)){
	    log_err("Did not receive COMM_UPDATE_COMMIT!  Cancelling update.\n");
	    msg_close(afd);
	    continue;
	  }
	  rename("/etc/cluster/cluster.conf-update", "/etc/cluster/cluster.conf");
	  update_required=1;
	  ch.comm_flags = COMM_UPDATE_COMMIT_ACK;
	  log_dbg("Sending COMM_UPDATE_COMMIT_ACK.\n");
	  msg_send(afd, &ch, sizeof(comm_header_t));
	  msg_close(afd);
	}
      }
    }
  }

  EXIT("cluster_communicator");
}

int start_cluster_monitor_thread(void){
  int error = 0;
  pthread_t	thread;
  
  ENTER("start_cluster_monitor_thread");

  error = pthread_create(&thread, NULL, (void *)cluster_communicator, NULL);
  if(error){
    log_err("Failed to create thread: %s\n", strerror(-error));
    goto fail;
  }

 fail:
  EXIT("start_cluster_monitor_thread");
  return error;
}


int update_remote_nodes(char *mem_doc, int doc_size){
  int i, fd;
  int error = 0;
  char *buffer = NULL;
  comm_header_t *ch, rch;
  uint64_t my_node_id;
  ENTER("update_remote_nodes");

  clu_local_nodeid(NULL, &my_node_id);

  buffer = malloc(sizeof(comm_header_t) + doc_size);
  if(!buffer){
    return -ENOMEM;
  }
  memset(buffer, 0, sizeof(comm_header_t) + doc_size);

  ch = (comm_header_t *)buffer;

  for(i=0; i < membership->cml_count; i++){
    log_dbg("  %s (%s)\n",
	    membership->cml_members[i].cm_name,
	    (membership->cml_members[i].cm_state == STATE_UP)?
	    "UP": "DOWN");
  }

  ch->comm_type = COMM_UPDATE;
  ch->comm_flags= COMM_UPDATE_NOTICE;
  ch->comm_payload_size = doc_size;

  memcpy(buffer+sizeof(comm_header_t), mem_doc, doc_size);

  log_dbg("doc_size = %d\n", doc_size);

  for(i=0; i < membership->cml_count; i++){
    if(membership->cml_members[i].cm_id == my_node_id){
      continue;
    }

    log_dbg("Sending COMM_UPDATE_NOTICE to %s\n", membership->cml_members[i].cm_name);

    fd = msg_open(membership->cml_members[i].cm_id, UPDATE_BASE_PORT, 0, 5);
    if(fd < 0){
      error = -errno;
      goto fail;
    }
    error = msg_send(fd, buffer, sizeof(comm_header_t) + doc_size);
    log_dbg("Sent doc and header (%d/%d bytes)\n",
	    error, sizeof(comm_header_t) + doc_size);
    error = msg_receive_timeout(fd, &rch, sizeof(comm_header_t), 5);
    if(error < 0){
      error = -errno;
      log_err("Failed to receive COMM_UPDATE_NOTICE_ACK from %s\n",
	      membership->cml_members[i].cm_name);
      goto fail;
    }
    log_dbg("COMM_UPDATE_NOTICE_ACK received from %s (%d of %d)\n",
	    membership->cml_members[i].cm_name,
	    i+1,
	    membership->cml_count-1);
    msg_close(fd);
  }

  log_dbg("Finished Phase 1... Starting Phase 2.\n");
  ch->comm_flags = COMM_UPDATE_COMMIT;

  for(i=0; i < membership->cml_count; i++){
    if(membership->cml_members[i].cm_id == my_node_id){
      continue;
    }    
    fd = msg_open(membership->cml_members[i].cm_id, UPDATE_BASE_PORT, 0, 5);
    if(fd < 0){
      error = -errno;
      goto fail;
    }
    error = msg_send(fd, buffer, sizeof(comm_header_t));
    error = msg_receive_timeout(fd, &rch, sizeof(comm_header_t), 5);
    if(error < 0){
      error = -errno;
      log_err("Failed to receive COMM_UPDATE_COMMIT_ACK from %s\n",
	      membership->cml_members[i].cm_name);
      goto fail;
    }
    log_dbg("COMM_UPDATE_COMMIT_ACK received from %s (%d of %d)\n",
	    membership->cml_members[i].cm_name,
	    i+1,
	    membership->cml_count-1);
    msg_close(fd);
    error = 0;
  }

 fail:
  if(buffer) free(buffer);
  EXIT("update_remote_nodes");
  return error;
}
