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
#include "misc.h"
#include "globals.h"

#include "magma.h"
#include "magmamsg.h"


static cluster_member_list_t *membership = NULL;

static int check_update_doc(xmlDocPtr tmp_doc){
  int error = 0;
  char *str1 = NULL;
  char *str2 = NULL;

  ENTER("check_update_doc");

  if(!(str1 = get_cluster_name(tmp_doc))){
    log_err("Unable to get cluster name from new config file.\n");
    error = -EINVAL;
    goto fail;
  }

  if(master_doc && master_doc->od_doc &&
     !(str2 = get_cluster_name(master_doc->od_doc))){
    log_dbg("Unable to get cluster name from current master doc.\n");
  }

  if(str2 && strcmp(str1, str2)){
    log_err("Cluster names for current and update configs do not match.\n");
    log_err("  Current cluster name:: <%s>\n", str2);
    log_err("  Proposed update name:: <%s>\n", str1);
    error = -EINVAL;
    goto fail;
  }
    
  if(master_doc && master_doc->od_doc &&
     (get_doc_version(tmp_doc) <= get_doc_version(master_doc->od_doc))){
    log_err("Proposed updated config file does not have greater version number.\n");
    log_err("  Current config_version :: %d\n",
	    get_doc_version(master_doc->od_doc));
    log_err("  Proposed config_version:: %d\n",
	    get_doc_version(tmp_doc));
    error = -EINVAL;
  }

 fail:
  if(str1){
    free(str1);
  }
  if(str2){
    free(str2);
  }
  EXIT("check_update_doc");
  return error;
}


static int handle_cluster_message(int fd){
  int error = 0;
  int afd= -1;
  int file_fd = -1;;
  int unlock=0;
  char *buffer = NULL;
  xmlDocPtr tmp_doc = NULL;
  comm_header_t ch;
  uint64_t nodeid;
  static uint64_t master_node=0;
  
  ENTER("handle_cluster_message");

  log_dbg("Cluster message on socket: %d\n", fd);
  if((afd = msg_accept(fd, 1, &nodeid)) < 0){
    log_sys_err("Failed to accept connection.\n");
    goto fail;
  }

  log_dbg("Accept socket: %d\n", afd);

  error = msg_peek(afd, &ch, sizeof(comm_header_t));
  if(error < 0){
    log_sys_err("Failed to receive message from %s\n",
		memb_id_to_name(membership,nodeid));
    goto fail;
  }

  log_dbg("Message (%d bytes) received from %s\n", error,
	  memb_id_to_name(membership,nodeid));
  if(ch.comm_type != COMM_UPDATE){
    log_err("Unexpected communication type... ignoring.\n");
    error = -EINVAL;
    goto fail;
  }

  if(ch.comm_flags == COMM_UPDATE_NOTICE){
    buffer = malloc(ch.comm_payload_size + sizeof(comm_header_t));
    if(!buffer){
      log_err("Unable to allocate space to perform update.\n");
      error = -ENOMEM;
      goto fail;
    }
	
    log_dbg("Updated config size:: %d\n", ch.comm_payload_size);

    error = msg_receive_timeout(afd, buffer, 
				ch.comm_payload_size+sizeof(comm_header_t), 5);

    if(error < 0){
      log_sys_err("Unable to retrieve updated config");
      goto fail;
    }

    pthread_mutex_lock(&update_lock);
    unlock=1;
    log_dbg("Got lock 0\n");
    
    tmp_doc = xmlParseMemory(buffer+sizeof(comm_header_t), ch.comm_payload_size);
    if(!tmp_doc){
      log_err("Unable to parse updated config file.\n");
      /* ATTENTION -- need better error code */
      error = -EIO;
      goto fail;
    }

    if((error = check_update_doc(tmp_doc)) < 0){
      goto fail;
    }

    file_fd = open("/etc/cluster/cluster.conf-update",
		   O_CREAT | O_WRONLY | O_TRUNC,
		   S_IRUSR | S_IRGRP);
    if(file_fd < 0){
      log_sys_err("Unable to open /etc/cluster/cluster.conf-update");
      error = file_fd;
      goto fail;
    }

    if((error = write(file_fd, buffer+sizeof(comm_header_t), ch.comm_payload_size)) < 0){
      log_sys_err("Unable to write /etc/cluster/cluster.conf-update");
      goto fail;
    }

    log_dbg("Upload of new config file from %s complete.\n",
	    memb_id_to_name(membership,nodeid));
    
    ch.comm_payload_size = 0;
    ch.comm_flags = COMM_UPDATE_NOTICE_ACK;
    log_dbg("Sending COMM_UPDATE_NOTICE_ACK.\n");
    if((error = msg_send(afd, &ch, sizeof(comm_header_t))) < 0){
      log_sys_err("Unable to send COMM_UPDATE_NOTICE_ACK.\n");
      goto fail;
    }
    master_node = nodeid;
    error = 0;
  } else if(ch.comm_flags == COMM_UPDATE_COMMIT){
    error = msg_receive_timeout(afd, &ch, sizeof(comm_header_t), 5);

    if(master_node != nodeid){
      log_err("COMM_UPDATE_COMMIT received from node other than initiator.\n");
      error = -EPERM;
      goto fail;
    }
    
    pthread_mutex_lock(&update_lock);
    unlock = 1;
    log_dbg("Got lock 1\n");
    
    tmp_doc = xmlParseFile("/etc/cluster/cluster.conf-update");
    if(!tmp_doc){
      log_err("Unable to parse updated config file.\n");
      /* ATTENTION -- need better error code */
      error = -EIO;
      goto fail;
    }

    if((error = check_update_doc(tmp_doc)) < 0){
      goto fail;
    }
    
    rename("/etc/cluster/cluster.conf-update", "/etc/cluster/cluster.conf");
    update_required=1;
    ch.comm_flags = COMM_UPDATE_COMMIT_ACK;
    log_dbg("Sending COMM_UPDATE_COMMIT_ACK.\n");
    if((error = msg_send(afd, &ch, sizeof(comm_header_t))) < 0){
      log_sys_err("Unable to send COMM_UPDATE_NOTICE_ACK.\n");
      goto fail;
    }
    error = 0;
  }
  
 fail:
  if(file_fd >= 0){
    close(file_fd);
  }
  if(afd >= 0){
    msg_close(afd);
  }
  if(buffer){
    free(buffer);
  }
  if(tmp_doc){
    xmlFreeDoc(tmp_doc);
  }
  if(unlock){
    pthread_mutex_unlock(&update_lock);
  }
  EXIT("handle_cluster_message");
  return error;
}




/**
 * handle_cluster_event
 * @fd: fd returned from clu_connect
 *
 * Returns: 0 on success, -1 on shutdown event
 */
static int handle_cluster_event(int fd){
  ENTER("handle_cluster_event");

  switch(clu_get_event(fd)) {
  case CE_NULL:
    log_dbg("-E- Spurious wakeup\n");
    break;
  case CE_SUSPEND:
    log_dbg("*E* Suspend activities\n");
    break;
  case CE_MEMB_CHANGE:
    log_dbg("*E* Membership change\n");
    break;
  case CE_QUORATE:
    log_dbg("*E* Quorum formed\n");
    quorate = 1;
    break;
  case CE_INQUORATE:
    log_dbg("*E* Quorum dissolved\n");
    quorate = 0;
    break;
  case CE_SHUTDOWN:
    log_dbg("*E* Node shutdown\n");
    quorate = 0;
    clu_disconnect(fd);
    EXIT("handle_cluster_event");
    return -1;
  default:
    log_dbg("-E- Unknown cluster event\n");
  }
  cml_free(membership);
  membership = clu_member_list(NULL);
  memb_resolve_list(membership, NULL);
  msg_update(membership);

  EXIT("handle_cluster_event");
  return 0;
}	


static void cluster_communicator(void){
  int cluster_fd = -1;
  int listen_fds[2], listeners;
  int fd;
  int error;
  fd_set rset;
  int max_fds, n;

  ENTER("cluster_communicator");

  if ((listeners = msg_listen(cluster_base_port, 0, listen_fds, 2)) <= 0) {
    log_err("Unable to setup update listener socket.\n");
    exit(EXIT_FAILURE);
  }

  for(n=0; n < listeners; n++){
    log_dbg("Listener[%d] = %d\n", n, listen_fds[n]);
  }

 restart:
  while(cluster_fd < 0){
    cluster_fd = clu_connect(NULL, 0);
  }

  log_dbg("cluster_fd = %d\n", cluster_fd);

  log_msg("Connected to cluster infrastruture via: %s\n", clu_plugin_version());

  quorate = (clu_quorum_status(NULL) & QF_QUORATE);
  log_msg("Initial status:: %s\n", (quorate)? "Quorate" : "Inquorate");

  membership = clu_member_list(NULL);
  msg_update(membership);
  memb_resolve_list(membership, NULL);

  while(1) {
    FD_ZERO(&rset);
    max_fds = msg_fill_fdset(&rset, MSG_ALL, MSGP_ALL);
    log_dbg("Waiting for cluster event.\n");
    n = select(max_fds+1, &rset, NULL, NULL, NULL);
    
    if(n < 0){
      log_sys_err("Select failed");
      continue;
    }
    log_dbg("There are %d cluster messages waiting.\n", n);

    while(n){
      log_dbg("There are %d messages remaining.\n", n);
      fd = msg_next_fd(&rset);
      if(fd == -1) { break; }
      n--;
      if(fd == cluster_fd){
	if(handle_cluster_event(fd)){
	  cluster_fd = -1;
	  goto restart;
	}
      } else {
	if((error = handle_cluster_message(fd)) < 0){
	  log_err("Error while responding to cluster message: %s\n", strerror(-error));
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

  pthread_mutex_init(&update_lock, NULL);

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
  int i, fd=-1;
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

  log_dbg("Starting Phase 1.\n");
  for(i=0; i < membership->cml_count; i++){
    if(membership->cml_members[i].cm_id == my_node_id){
      continue;
    }

    log_dbg("Sending COMM_UPDATE_NOTICE to %s\n", membership->cml_members[i].cm_name);

    fd = msg_open(membership->cml_members[i].cm_id, cluster_base_port, 0, 5);
    log_dbg("Master update socket P1 (to %s) = %d\n",
	    membership->cml_members[i].cm_name, fd);
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
      msg_close(fd);
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
    fd = msg_open(membership->cml_members[i].cm_id, cluster_base_port, 0, 5);
    log_dbg("Master update socket P2 (to %s) = %d\n",
	    membership->cml_members[i].cm_name, fd);
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
      msg_close(fd);
      goto fail;
    }
    log_dbg("COMM_UPDATE_COMMIT_ACK received from %s (%d of %d)\n",
	    membership->cml_members[i].cm_name,
	    i+1,
	    membership->cml_count-1);
    msg_close(fd);
    error = 0;
  }
  log_dbg("Finished Phase 2.\n");

 fail:
  if(buffer) free(buffer);
  EXIT("update_remote_nodes");
  return error;
}
