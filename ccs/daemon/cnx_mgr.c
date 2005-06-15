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
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include "log.h"
#include "comm_headers.h"
#include "debug.h"
#include "misc.h"
#include "globals.h"

extern int no_manager_opt;

typedef struct open_connection_s {
  char *oc_cwp;
  char *oc_query;
  int oc_index;
  open_doc_t *oc_odoc;
  xmlXPathContextPtr oc_ctx;
} open_connection_t;

/* ATTENTION: need to lock on this if we start forking the daemon **
**  Also would need to create a shared memory area for open cnx's */
#define MAX_OPEN_CONNECTIONS 10
static open_connection_t **ocs = NULL;

static int _update_config(char *location){
  int error = 0;
  int v1=0, v2=0;
  open_doc_t *tmp_odoc = NULL;
  xmlDocPtr tmp_doc = NULL;

  ENTER("_update_config");

  tmp_doc = xmlParseFile(location);
  if(!tmp_doc){
    log_err("Unable to parse %s\n", location);
    error = -EINVAL;
    goto fail;
  } else if((v2 = get_doc_version(tmp_doc)) < 0){
    log_err("Unable to get config_version from cluster.conf.\n");
    error = v2;
    goto fail;
  } else if(master_doc && master_doc->od_doc){
    v1 = get_doc_version(master_doc->od_doc);
    if(v1 >= v2){
      log_err("cluster.conf on-disk version is <= to in-memory version.\n");
      log_err(" On-disk version   : %d\n", v2);
      log_err(" In-memory version : %d\n", v1);
      error = -EPERM;
      goto fail;
    }
  } else {
    v1 = 0;
  }

  if(!(tmp_odoc = malloc(sizeof(open_doc_t)))){
    error = -ENOMEM;
    goto fail;
  }
  memset(tmp_odoc, 0, sizeof(open_doc_t));

  tmp_odoc->od_doc = tmp_doc;

  log_dbg("There are %d references open on version %d of the config file.\n",
	  (master_doc)?master_doc->od_refs:0, v1);
  if(master_doc && !master_doc->od_refs){
    log_dbg("Freeing version %d\n", v1);
    xmlFreeDoc(master_doc->od_doc);
    free(master_doc);
    master_doc = tmp_odoc;
  } else {
    master_doc = tmp_odoc;
  }

  log_msg("Update of cluster.conf complete (version %d -> %d).\n", v1, v2);
 fail:
  if(tmp_odoc != master_doc){
    free(tmp_odoc);
  }
  if(tmp_doc != master_doc->od_doc){
    xmlFreeDoc(tmp_doc);
  }


  EXIT("_update_config");
  return error;
}


static int update_config(void){
  int error = 0;
  ENTER("update_config");

  /* If update_required is set, it means that there is still a pending **
  ** update.  We need to pull this one in before doing anything else.  */
  if(update_required){
    error = _update_config("/etc/cluster/.cluster.conf");
    update_required = 0;
    if(error){
      log_err("Previous update could not be completed.\n");
      goto fail;
    }
  }

 fail:
  EXIT("update_config");
  return error;
}

/**
 * broadcast_for_doc
 *
 * Returns: 0 on success, < 0 on error
 */
static int broadcast_for_doc(char *cluster_name, int blocking){
  int opt;
  int error = 0;
  int retry = 5;
  int sfd = -1;
  int trueint;
  int v1, v2;
  int write_to_disk = 0;
  char *tmp_name = NULL;
  struct sockaddr_storage addr, recv_addr;
  struct sockaddr_in *addr4 = (struct sockaddr_in *)&addr;
  struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&addr;
  int addr_size = 0;
  int len=sizeof(struct sockaddr_storage);
  comm_header_t *ch = NULL;
  char *bdoc = NULL;
  fd_set rset;
  struct timeval tv;
  xmlDocPtr tmp_doc = NULL;

  ENTER("broadcast_for_doc");

 try_again:
  if(!master_doc){
    log_err("No master_doc!!!\n");
    exit(EXIT_FAILURE);
  }

  if(quorate && !cluster_name){
    log_err("Node is part of quorate cluster, but the cluster name is unknown.\n");
    log_err(" Unable to validate remote config files.  Refusing connection.\n");
    error = -ECONNREFUSED;
    goto fail;
  }

  ch = malloc(sizeof(comm_header_t));
  if(!ch){
    error = -ENOMEM;
    goto fail;
  }
  memset(ch, 0, sizeof(comm_header_t));

  if(IPv6 && (sfd = socket(PF_INET6, SOCK_DGRAM, IPPROTO_UDP)) <0){
    log_sys_err("Unable to create IPv6 socket");
    error = -errno;
    goto fail;
  }

  if(!IPv6 && ((sfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)){
    log_sys_err("Unable to create socket for broadcast");
    error = -errno;
    goto fail;
  }

  memset(&addr, 0, sizeof(struct sockaddr_storage));

  trueint = 1;
  if(IPv6){
    struct ipv6_mreq mreq;

    addr6->sin6_family = AF_INET6;
    addr6->sin6_port = htons(backend_port);

    if(!multicast_address || !strcmp(multicast_address, "default")){
      log_dbg("Trying IPv6 multicast (default).\n");
      if(inet_pton(AF_INET6, "ff02::3:1", &(addr6->sin6_addr)) <= 0){
	log_sys_err("Unable to convert multicast address");
	error = -errno;
	goto fail;
      }
    } else {
      log_dbg("Trying IPv6 multicast (%s).\n", multicast_address);
      if(inet_pton(AF_INET6, multicast_address, &(addr6->sin6_addr)) <= 0){
	log_sys_err("Unable to convert multicast address");
	error = -errno;
	goto fail;
      }
    }

    memcpy(&mreq.ipv6mr_multiaddr, &(addr6->sin6_addr), sizeof(struct in6_addr));
    mreq.ipv6mr_interface = 0;
    opt = 0;

    if(setsockopt(sfd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
                  &opt, sizeof(opt)) < 0){
      log_err("Unable to %s loopback.\n", opt?"SET":"UNSET");
      error = -errno;
      goto fail;
    }
  } else {
    addr4->sin_family = AF_INET;
    addr4->sin_port = htons(backend_port);
    if(!multicast_address){
      log_dbg("Trying IPv4 broadcast.\n");

      addr4->sin_addr.s_addr = INADDR_BROADCAST;
      if((error = setsockopt(sfd, SOL_SOCKET, SO_BROADCAST, &trueint, sizeof(int)))){
	log_sys_err("Unable to set socket options");
	error = -errno;
	goto fail;
      } else {
	log_dbg("  Broadcast enabled.\n");
      }
    } else {
      if(!strcmp(multicast_address, "default")){
	log_dbg("Trying IPv4 multicast (default).\n");
	if(inet_pton(AF_INET, "224.0.2.5", &(addr4->sin_addr)) <= 0){
	  log_sys_err("Unable to convert multicast address");
	  error = -errno;
	  goto fail;
	}
      } else {
	log_dbg("Trying IPv4 multicast (%s).\n", multicast_address);
	if(inet_pton(AF_INET, multicast_address, &(addr4->sin_addr)) <= 0){
	  log_sys_err("Unable to convert multicast address");
	  error = -errno;
	  goto fail;
	}
      }
      opt = 0;
      setsockopt(sfd, IPPROTO_IP, IP_MULTICAST_LOOP, &opt, sizeof(opt));
      if(setsockopt(sfd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0){
	log_sys_err("Unable to set multicast threshold.\n");
      }
    }
  }
  addr_size = IPv6? sizeof(struct sockaddr_in6):sizeof(struct sockaddr_in);

  FD_ZERO(&rset);

  do {
    ch->comm_type = COMM_BROADCAST;

    log_dbg("Sending broadcast.\n");

    if(sendto(sfd, (char *)ch, sizeof(comm_header_t), 0,
	      (struct sockaddr *)&addr, addr_size) < 0){
      log_sys_err("Unable to perform sendto");
      if(retry > 0){
	retry--;
	close(sfd);
	free(ch);
	sleep(2);
	goto try_again;
      } else {
	error = -errno;
	goto fail;
      }
    }

    srandom(getpid());
    FD_SET(sfd, &rset);
    tv.tv_sec = 0;
    
    tv.tv_usec = 250000 + (random()%500000);
    log_dbg("Select waiting %ld usec\n", tv.tv_usec);
    while((error = select(sfd+1, &rset, NULL,NULL, &tv))){
      log_dbg("Select returns %d\n", error);
      if(error < 0){
	log_sys_err("Select failed");
	error = -errno;
	goto fail;
      }
      if(error){
	log_dbg("Checking broadcast response.\n");
	error = 0;
	recvfrom(sfd, (char *)ch, sizeof(comm_header_t), MSG_PEEK,
		 (struct sockaddr *)&recv_addr, (socklen_t *)&len);
	if(!ch->comm_payload_size || ch->comm_error){
	  /* clean out this reply by not using MSG_PEEK */
	  recvfrom(sfd, (char *)ch, sizeof(comm_header_t), 0,
		   (struct sockaddr *)&recv_addr, (socklen_t *)&len);
	  error = -ENODATA;
	  FD_SET(sfd, &rset);
	  goto reset_timer;
	}
	bdoc = malloc(ch->comm_payload_size + sizeof(comm_header_t));
	if(!bdoc){
	  error = -ENOMEM;
	  goto fail;
	}
	memset(bdoc, 0, ch->comm_payload_size + sizeof(comm_header_t));
	/* ATTENTION -- potential for incomplete package */
	recvfrom(sfd, bdoc, ch->comm_payload_size + sizeof(comm_header_t),
		 0, (struct sockaddr *)&recv_addr, &len);
	tmp_doc = xmlParseMemory(bdoc+sizeof(comm_header_t),
				 ch->comm_payload_size);
	if(!tmp_doc){
	  log_err("Unable to parse remote cluster.conf.\n");
	  free(bdoc); bdoc = NULL;
	  goto reset_timer;
	}

	tmp_name = get_cluster_name(tmp_doc);
	log_dbg("  Given cluster name = %s\n", cluster_name);
	log_dbg("  Remote cluster name= %s\n", tmp_name);
	if(!tmp_name){
	  log_err("Unable to find cluster name in remote cluster.conf.\n");
	  free(bdoc); bdoc = NULL;
	  xmlFreeDoc(tmp_doc); tmp_doc = NULL;
	  goto reset_timer;
	} else if(cluster_name && strcmp(cluster_name, tmp_name)){
	  log_dbg("Remote and local cluster.conf have different cluster names.\n");
	  log_dbg("Skipping...\n");
	  free(tmp_name); tmp_name = NULL;
	  free(bdoc); bdoc = NULL;
	  xmlFreeDoc(tmp_doc); tmp_doc = NULL;
	  goto reset_timer;
	}
	free(tmp_name); tmp_name = NULL;
	if(!master_doc->od_doc){
	  if((v2 = get_doc_version(tmp_doc)) >= 0){
	    log_msg("Remote copy of cluster.conf (version = %d) found.\n", v2);
	    master_doc->od_doc = tmp_doc;
	    tmp_doc = NULL;
	    write_to_disk = 1;
	  }
	} else {
	  if(((v1 = get_doc_version(master_doc->od_doc)) >= 0) &&
	     ((v2 = get_doc_version(tmp_doc)) >= 0)){
	    if(ch->comm_flags & COMM_BROADCAST_FROM_QUORATE){
	      log_msg("Remote copy of cluster.conf is from quorate node.\n");
	      log_msg(" Local version # : %d\n", v1);
	      log_msg(" Remote version #: %d\n", v2);
	      if(v1 != v2){
		log_msg("Switching to remote copy.\n");
	      }
	      if(master_doc->od_refs){
		open_doc_t *tmp_odoc;
		if(!(tmp_odoc = malloc(sizeof(open_doc_t)))){
		  error = -ENOMEM;
		  goto fail;
		}
		memset(tmp_odoc, 0, sizeof(open_doc_t));
		tmp_odoc->od_doc = tmp_doc;
		master_doc = tmp_odoc;
	      } else {
		xmlFreeDoc(master_doc->od_doc);
		master_doc->od_doc = tmp_doc;
	      }
	      tmp_doc = NULL;
	      write_to_disk = 1;
	      goto out;
	    } else if(v2 > v1){
	      log_msg("Remote copy of cluster.conf is newer than local copy.\n");
	      log_msg(" Local version # : %d\n", v1);
	      log_msg(" Remote version #: %d\n", v2);
	      if(master_doc->od_refs){
		open_doc_t *tmp_odoc;
		if(!(tmp_odoc = malloc(sizeof(open_doc_t)))){
		  error = -ENOMEM;
		  goto fail;
		}
		memset(tmp_odoc, 0, sizeof(open_doc_t));
		tmp_odoc->od_doc = tmp_doc;
		master_doc = tmp_odoc;
	      } else {
		xmlFreeDoc(master_doc->od_doc);
		master_doc->od_doc = tmp_doc;
	      }
	      tmp_doc = NULL;
	      write_to_disk = 1;
	    }
	  } else {
	    xmlFreeDoc(tmp_doc);
	    tmp_doc = NULL;
	  }
	}
	free(bdoc); bdoc = NULL;
      }
      FD_SET(sfd, &rset);
      /* select will alter the timeout */
    reset_timer:
      tv.tv_sec = 0;
      tv.tv_usec = 250000 + (random()%500000);
      log_dbg("Select waiting %ld usec\n", tv.tv_usec);
    }
  } while(blocking && !master_doc);
 out:
  if(error){
    goto fail;
  }

  if(write_to_disk){
    struct stat stat_buf;
    FILE *f;
    /* We did not have a copy available or we found a newer one, so write it out */

    /* ATTENTION -- its bad if we fail here, because we have an in-memory version **
    ** but it has not been written to disk....................................... */
    if(stat("/etc/cluster", &stat_buf)){
      if(mkdir("/etc/cluster", S_IRWXU | S_IRWXG)){
	log_sys_err("Unable to create directory /etc/cluster");
	error = -errno;
	goto fail;
      }
    } else if(!S_ISDIR(stat_buf.st_mode)){
      log_err("/etc/cluster is not a directory.\n");
      error = -ENOTDIR;
      goto fail;
    }
    f = fopen("/etc/cluster/cluster.conf", "w");
    if(!f){
      log_sys_err("Unable to open /etc/cluster/cluster.conf");
      error = -errno;
      goto fail;
    }
    if(xmlDocDump(f, master_doc->od_doc) < 0){
      error = -EIO;
      fclose(f);
      goto fail;
    }
    fclose(f);
  }

 fail:
  if(ch) free(ch);
  if(bdoc) free(bdoc);
  if(tmp_doc) xmlFreeDoc(tmp_doc);
  if(sfd >= 0) close(sfd);
  EXIT("broadcast_for_doc");
  return error;
}

/**
 * process_connect: process a connect request
 * @afd: accepted socket connection
 * @cluster_name: optional cluster name
 *
 * Returns: 0 on success, < 0 on error
 */
static int process_connect(comm_header_t *ch, char *cluster_name){
  int i=0, error = 0;
  int bcast_needed = 0;
  char *tmp_name = NULL;
  

  ENTER("process_connect");

  ch->comm_payload_size = 0;

  log_dbg("Given cluster name is = %s\n", cluster_name);

  if(!ocs){
    /* this will never be freed - unless exit */
    ocs = malloc(sizeof(open_connection_t *)*MAX_OPEN_CONNECTIONS);
    if(!ocs){
      error = -ENOMEM;
      goto fail;
    }
    memset(ocs, 0, sizeof(open_connection_t *)*MAX_OPEN_CONNECTIONS);
  }

  if(!quorate && !(ch->comm_flags & COMM_CONNECT_FORCE)){
    log_msg("Cluster is not quorate.  Refusing connection.\n");
    error = -ECONNREFUSED;
    goto fail;
  }

  if(!master_doc){
    /* ATTENTION -- signal could come at any time.  It may be better to **
    ** malloc to different var, then copy to master_doc when done       */
    master_doc = malloc(sizeof(open_doc_t));
    if(!master_doc){
      error = -ENOMEM;
      goto fail;
    }
    memset(master_doc, 0, sizeof(open_doc_t));
  }

  if(!master_doc->od_doc){
    master_doc->od_doc = xmlParseFile("/etc/cluster/cluster.conf");
    if(!master_doc->od_doc){
      log_msg("Unable to parse %s\n", "/etc/cluster/cluster.conf");
      log_msg("Searching cluster for valid copy.\n");
    } else if((error = get_doc_version(master_doc->od_doc)) < 0){
      log_err("Unable to get config_version from cluster.conf.\n");
      log_err("Discarding data and searching for valid copy.\n");
      xmlFreeDoc(master_doc->od_doc);
      master_doc->od_doc = NULL;
    } else if(!(tmp_name = get_cluster_name(master_doc->od_doc))){
      log_err("Unable to get cluster name from cluster.conf.\n");
      log_err("Discarding data and searching for valid copy.\n");
      xmlFreeDoc(master_doc->od_doc);
      master_doc->od_doc = NULL;
    } else if(cluster_name && strcmp(cluster_name, tmp_name)){
      log_err("Given cluster name does not match local cluster.conf.\n");
      log_err("Discarding data and searching for matching copy.\n");
      xmlFreeDoc(master_doc->od_doc);
      master_doc->od_doc = NULL;
      free(tmp_name); tmp_name = NULL;
    } else {  /* Either the names match, or a name wasn't specified. */
      log_msg("cluster.conf (cluster name = %s, version = %d) found.\n",
	      tmp_name, error);
      /* We must check with the others to make sure this is valid. */
    }
    if (!no_manager_opt)
      bcast_needed = 1;
    error = 0;
  } else {
    tmp_name = get_cluster_name(master_doc->od_doc);

    /* ATTENTION -- if not quorate, consider swapping out in-memory config **
    ** for the config of the name specified............................... */

    if(cluster_name && strcmp(cluster_name, tmp_name)){
      log_err("Request for cluster.conf with cluster name, %s\n", cluster_name);
      log_err(" However, a cluster.conf with cluster name, %s, is already loaded.\n",
	      tmp_name);
      error = -EINVAL;
      goto fail;
    }
    if(!quorate){
      bcast_needed = 1;
    }
  }
  
  if(cluster_name && !tmp_name){
    tmp_name = strdup(cluster_name);
    if(!tmp_name){
      error = -ENOMEM;
      goto fail;
    }
  }

  log_dbg("Blocking is %s.\n",
	  (ch->comm_flags & COMM_CONNECT_BLOCKING)? "SET": "UNSET");
  log_dbg("Flags = 0x%x\n", ch->comm_flags);

  /* Need to broadcast regardless (unless quorate) to check version # */
  if(bcast_needed){
    log_dbg("Broadcast is neccessary.\n");
  }
  if(bcast_needed &&
     (error = broadcast_for_doc(tmp_name, ch->comm_flags & COMM_CONNECT_BLOCKING)) &&
     !master_doc->od_doc){
    log_err("Broadcast for config file failed: %s\n", strerror(-error));
    goto fail;
  }
  error = 0;

  if(!master_doc || !master_doc->od_doc){
    log_err("The appropriate config file could not be loaded.\n");
    error = -ENODATA;
    goto fail;
  }

  if(update_required){
    log_dbg("Update is required.\n");
    if((error = update_config())){
      log_err("Failed to update config file, required by cluster.\n");
      /* ATTENTION -- remove all open_doc_t's ? */
      goto fail;
    }
  }

  for(i=0; i < MAX_OPEN_CONNECTIONS; i++){
    if(!ocs[i]){
      break;
    }
  }

  if(i >= MAX_OPEN_CONNECTIONS){
    error = -EAGAIN;
    goto fail;
  }

  ocs[i] = (open_connection_t *)malloc(sizeof(open_connection_t));
  if(!ocs[i]){
    error = -ENOMEM;
    goto fail;
  }

  memset(ocs[i], 0, sizeof(open_connection_t));

  master_doc->od_refs++;
  ocs[i]->oc_odoc = master_doc;
  ocs[i]->oc_ctx = xmlXPathNewContext(ocs[i]->oc_odoc->od_doc);
  if(!ocs[i]->oc_ctx){
    ocs[i]->oc_odoc->od_refs--;
    free(ocs[i]);
    log_err("Error: unable to create new XPath context.\n");
    error = -EIO;  /* ATTENTION -- what should this be? */
    goto fail;
  }

  /* return desc to requestor */
  
 fail:
  if(master_doc && master_doc->od_doc == NULL){
    free(master_doc);
    master_doc = NULL;
  }
  if(tmp_name){
    free(tmp_name);
  }
  if(error){
    ch->comm_error = error;
  } else {
    ch->comm_desc = i;
  }
  EXIT("process_connect");
  return error;
}


/**
 * process_disconnect: close an open session
 * @afd: accepted socket connection
 * @desc: descriptor describing the open connection
 *
 * This fuction frees all memory associated with an open session.
 *
 * Returns: 0 on success, < 0 on error
 */
static int process_disconnect(comm_header_t *ch){
  open_doc_t *tmp_odoc;
  int desc = ch->comm_desc;
  int error=0;
  ENTER("process_disconnect");

  ch->comm_payload_size = 0;

  if(desc < 0 || desc >= MAX_OPEN_CONNECTIONS){
    log_err("Invalid descriptor specified (%d).\n", desc);
    log_err("Someone may be attempting something evil.\n");
    error = -EBADR;
    goto fail;
  }

  if(!ocs || !ocs[desc]){
    /* send failure to requestor ? */
    log_err("Attempt to close an unopened CCS descriptor (%d).\n", desc);

    error = -EBADR;
    goto fail;
  } else {
    if(ocs[desc]->oc_ctx){
      xmlXPathFreeContext(ocs[desc]->oc_ctx);
    }
    if(ocs[desc]->oc_cwp){
      free(ocs[desc]->oc_cwp);
    }
    if(ocs[desc]->oc_query){
      free(ocs[desc]->oc_query);
    }
    tmp_odoc = ocs[desc]->oc_odoc;
    if(tmp_odoc->od_refs < 1){
      log_err("Number of references on an open doc should never be < 1.\n");
      log_err("This is a fatal error.  Exiting...\n");
      exit(EXIT_FAILURE);
    }
    if(tmp_odoc != master_doc && tmp_odoc->od_refs == 1){
      log_dbg("No more references on version %d of config file, freeing...\n",
	      get_doc_version(tmp_odoc->od_doc));
      xmlFreeDoc(tmp_odoc->od_doc);
      free(tmp_odoc);
    } else {
      tmp_odoc->od_refs--;
    }

    free(ocs[desc]);
    ocs[desc] = NULL;
  }

 fail:
  if(error){
    ch->comm_error = error;
  } else {
    ch->comm_desc = -1;
  }
  EXIT("process_disconnect");
  return error;
}

/*
 * _process_get
 * @ch
 * @payload
 *
 * This function runs the xml query.  If the query is different from the
 * previous query, it will always fill the payload with the first match.
 * If the current query and the previous query are the same, it fills the
 * payload with next match.  If the last of all possible matches was
 * returned by the previous query and the current query is the same,
 * the payload will be filled with the 1st match and 1 will be returned
 * as the result of the function.
 *
 * Returns: -EXXX on error, 1 if restarting list, 0 otherwise
 */
static int _process_get(comm_header_t *ch, char **payload){
  int error = 0;
  xmlXPathObjectPtr obj = NULL;
  char *query = NULL;

  ENTER("_process_get");
  if(!ch->comm_payload_size){
    log_err("process_get: payload size is zero.\n");
    error = -EINVAL;
    goto fail;
  }

  if(ch->comm_desc < 0 || ch->comm_desc >= MAX_OPEN_CONNECTIONS){
    log_err("Invalid descriptor specified (%d).\n", ch->comm_desc);
    log_err("Someone may be attempting something evil.\n");
    error = -EBADR;
    goto fail;
  }

  if(!ocs || !ocs[ch->comm_desc]){
    log_err("process_get: Invalid connection descriptor received.\n");
    error = -EBADR;
    goto fail;
  }

  if(ocs[ch->comm_desc]->oc_query && !strcmp(*payload,ocs[ch->comm_desc]->oc_query)){
    ocs[ch->comm_desc]->oc_index++;
    log_dbg("Index = %d\n",ocs[ch->comm_desc]->oc_index);
    log_dbg(" Query = %s\n", *payload);
  } else {
    log_dbg("Index reset (new query).\n");
    log_dbg(" Query = %s\n", *payload);
    ocs[ch->comm_desc]->oc_index = 0;
    if(ocs[ch->comm_desc]->oc_query){
      free(ocs[ch->comm_desc]->oc_query);
    }
    ocs[ch->comm_desc]->oc_query = (char *)strdup(*payload);
  }

  /* ATTENTION -- should path expansion go before index inc ? */
  if(((ch->comm_payload_size > 1) &&
      ((*payload)[0] == '/')) ||
     !ocs[ch->comm_desc]->oc_cwp){
    log_dbg("Query involves absolute path or cwp is not set.\n");
    query = (char *)strdup(*payload);
    if(!query){
      error = -ENOMEM;
      goto fail;
    }
  } else {
    /* +2 because of NULL and '/' character */
    log_dbg("Query involves relative path.\n");
    query = malloc(strlen(*payload)+strlen(ocs[ch->comm_desc]->oc_cwp)+2);
    if(!query){
      error = -ENOMEM;
      goto fail;
    }
    sprintf(query, "%s/%s", ocs[ch->comm_desc]->oc_cwp, *payload);
  }

  obj = xmlXPathEvalExpression(query, ocs[ch->comm_desc]->oc_ctx);
  if(obj){
    log_dbg("Obj type  = %d (%s)\n", obj->type, (obj->type == 1)?"XPATH_NODESET":"");
    log_dbg("Number of matches: %d\n", (obj->nodesetval)?obj->nodesetval->nodeNr:0);
    if(obj->nodesetval && (obj->nodesetval->nodeNr > 0) ){
      xmlNodePtr node;
      int size=0;
      int nnv=0; /* name 'n' value */

      if(ocs[ch->comm_desc]->oc_index >= obj->nodesetval->nodeNr){
	ocs[ch->comm_desc]->oc_index = 0;
	error = 1;
	log_dbg("Index reset to zero (end of list).\n");
      }
	  
      node = obj->nodesetval->nodeTab[ocs[ch->comm_desc]->oc_index];
	
      log_dbg("Node (%s) type = %d (%s)\n", node->name, node->type,
	      (node->type == 1)? "XML_ELEMENT_NODE":
	      (node->type == 2)? "XML_ATTRIBUTE_NODE":"");

      if(!node || !node->children ||
         !node->children->content || !strlen(node->children->content)){
	log_dbg("No content found.\n");
	error = -ENODATA;
	goto fail;
      }

      log_dbg("Query results:: %s\n", node->children->content);

      if(((node->type == XML_ATTRIBUTE_NODE) && strstr(query, "@*")) ||
	 ((node->type == XML_ELEMENT_NODE) && strstr(query, "child::*"))){
	/* add on the trailing NULL and the '=' separator for a list of attrs
	   or an element node + CDATA*/
	size = strlen(node->children->content)+strlen(node->name)+2;
	nnv= 1;
      } else {
	size = strlen(node->children->content)+1;
      }

      if(size <= ch->comm_payload_size){  /* do we already have enough space? */
	log_dbg("No extra space needed.\n");
	if(nnv){
	  sprintf(*payload, "%s=%s", node->name,node->children->content);
	}else {
	  sprintf(*payload, "%s", node->children->content);
	}
      } else {
	log_dbg("Extra space needed.\n");
	free(*payload);
	*payload = (char *)malloc(size);
	if(!*payload){
	  error = -ENOMEM;
	  goto fail;
	}
	if(nnv){
	  sprintf(*payload, "%s=%s", node->name, node->children->content);
	}else {
	  sprintf(*payload, "%s", node->children->content);
	}
      }
      ch->comm_payload_size = size;
    } else {
      log_dbg("No nodes found.\n");
      ch->comm_payload_size = 0;
      error = -ENODATA;
      goto fail;
    }
  } else {
    log_err("Error: unable to evaluate xpath query \"%s\"\n", *payload);
    error = -EINVAL;
    goto fail;
  }

 fail:
  if(obj){
    xmlXPathFreeObject(obj);
  }
  if(error < 0){
    ch->comm_error = error;
    ch->comm_payload_size = 0;
  }
  if(query) { free(query); }
  EXIT("_process_get");
  return error;
}

static int process_get(comm_header_t *ch, char **payload){
  int error;
  ENTER("process_get");

  error = _process_get(ch, payload);

  EXIT("process_get");
  return (error < 0)? error: 0;  
}

static int process_get_list(comm_header_t *ch, char **payload){
  int error;
  ENTER("process_get_list");

  error = _process_get(ch, payload);
  if(error){
    ch->comm_payload_size = 0;
    if(ocs && ocs[ch->comm_desc])
      ocs[ch->comm_desc]->oc_index = -1;
  }

  EXIT("process_get_list");
  return (error < 0)? error: 0;
}

static int process_set(comm_header_t *ch, char *payload){
  int error = 0;

  ENTER("process_set");
  if(!ch->comm_payload_size){
    log_err("process_set: payload size is zero.\n");
    error = -EINVAL;
    goto fail;
  }

  if(ch->comm_desc < 0 || ch->comm_desc >= MAX_OPEN_CONNECTIONS){
    log_err("Invalid descriptor specified (%d).\n", ch->comm_desc);
    log_err("Someone may be attempting something evil.\n");
    error = -EBADR;
    goto fail;
  }

  if(!ocs || !ocs[ch->comm_desc]){
    log_err("process_set: Invalid connection descriptor received.\n");
    error = -EBADR;
    goto fail;
  }

  error = -ENOSYS;  

 fail:
  free(payload);
  ch->comm_payload_size = 0;
  if(error){
    ch->comm_error = error;
  }
  EXIT("process_set");
  return error;
}


static int process_get_state(comm_header_t *ch, char **payload){
  int error = 0;
  char *load = NULL;

  ENTER("process_get_state");
  if(ch->comm_payload_size){
    log_err("process_get_state: payload size is nonzero.\n");
    error = -EINVAL;
    goto fail;
  }

  if(ch->comm_desc < 0 || ch->comm_desc >= MAX_OPEN_CONNECTIONS){
    log_err("Invalid descriptor specified (%d).\n", ch->comm_desc);
    log_err("Someone may be attempting something evil.\n");
    error = -EBADR;
    goto fail;
  }

  if(!ocs || !ocs[ch->comm_desc]){
    log_err("process_get_state: Invalid connection descriptor received.\n");
    error = -EBADR;
    goto fail;
  }

  if(ocs[ch->comm_desc]->oc_cwp && ocs[ch->comm_desc]->oc_query){
    int size = strlen(ocs[ch->comm_desc]->oc_cwp) +
      strlen(ocs[ch->comm_desc]->oc_query) + 2;
    log_dbg("Both cwp and query are set.\n");
    load = malloc(size);
    if(!load){
      error = -ENOMEM;
      goto fail;
    }
    strcpy(load, ocs[ch->comm_desc]->oc_cwp);
    strcpy(load+strlen(ocs[ch->comm_desc]->oc_cwp)+1, ocs[ch->comm_desc]->oc_query);
    ch->comm_payload_size = size;
  } else if(ocs[ch->comm_desc]->oc_cwp){
    log_dbg("Only cwp is set.\n");
    load = (char *)strdup(ocs[ch->comm_desc]->oc_cwp);
    if(!load){
      error = -ENOMEM;
      goto fail;
    }
    ch->comm_payload_size = strlen(load)+1;
  } else if(ocs[ch->comm_desc]->oc_query){
    int size = strlen(ocs[ch->comm_desc]->oc_query) + 2;
    log_dbg("Only query is set.\n");
    load = malloc(size);
    if(!load){
      error = -ENOMEM;
      goto fail;
    }
    memset(load, 0, size);
    strcpy(load+1, ocs[ch->comm_desc]->oc_query);
    ch->comm_payload_size = size;
  }

  *payload = load;

 fail:
  if(error){
    if(load) { free(load); }
    ch->comm_error = error;
    ch->comm_payload_size = 0;
  }
  EXIT("process_get_state");
  return error;
}


static int process_set_state(comm_header_t *ch, char *payload){
  int error = 0;

  ENTER("process_set_state");
  if(!ch->comm_payload_size){
    log_err("process_set_state: payload size is zero.\n");
    error = -EINVAL;
    goto fail;
  }

  if(ch->comm_desc < 0 || ch->comm_desc >= MAX_OPEN_CONNECTIONS){
    log_err("Invalid descriptor specified (%d).\n", ch->comm_desc);
    log_err("Someone may be attempting something evil.\n");
    error = -EBADR;
    goto fail;
  }

  if(!ocs || !ocs[ch->comm_desc]){
    log_err("process_set_state: Invalid connection descriptor received.\n");
    error = -EBADR;
    goto fail;
  }

  if(ocs[ch->comm_desc]->oc_cwp){
    free(ocs[ch->comm_desc]->oc_cwp);
    ocs[ch->comm_desc]->oc_cwp = NULL;
  }

  if((ch->comm_flags & COMM_SET_STATE_RESET_QUERY) && ocs[ch->comm_desc]->oc_query){
    free(ocs[ch->comm_desc]->oc_query);
    ocs[ch->comm_desc]->oc_query = NULL;
  }

  ocs[ch->comm_desc]->oc_cwp = (char *)strdup(payload);

 fail:
  ch->comm_payload_size = 0;
  if(error){
    ch->comm_error = error;
  }

  EXIT("process_set_state");
  return error;
}


/**
 * process_request
 * @afd
 *
 * This function operates as a switch, passing the request to the
 * appropriate function.
 *
 * Returns: 0 on success, < 0 on error
 */
int process_request(int afd){
  int error=0;
  comm_header_t *ch = NULL, *tmp_ch;
  char *payload = NULL;
  
  ENTER("process_request");

  if(!(ch = (comm_header_t *)malloc(sizeof(comm_header_t)))){
    error = -ENOMEM;
    goto fail;
  }

  error = read(afd, ch, sizeof(comm_header_t));
  if(error < 0){
    log_sys_err("Unable to read comm_header_t");
    goto fail;
  } else if(error < sizeof(comm_header_t)){
    log_err("Unable to read complete comm_header_t.\n");
    error = -EBADE;
    goto fail;
  }

  if(ch->comm_payload_size){
    if(!(payload = (char *)malloc(ch->comm_payload_size))){
      error = -ENOMEM;
      goto fail;
    }
    error = read(afd, payload, ch->comm_payload_size);
    if(error < 0){
      log_sys_err("Unable to read payload");
      goto fail;
    } else if(error < ch->comm_payload_size){
      log_err("Unable to read complete payload.\n");
      error = -EBADE;
      goto fail;
    }
  }

  switch(ch->comm_type){
  case COMM_CONNECT:
    if((error = process_connect(ch, payload)) < 0){
      log_err("Error while processing connect: %s\n", strerror(-error));
      goto fail;
    }
    break;
  case COMM_DISCONNECT:
    if((error = process_disconnect(ch)) < 0){
      log_err("Error while processing disconnect: %s\n", strerror(-error));
      goto fail;
    }
    break;
  case COMM_GET:
    if((error = process_get(ch, &payload)) < 0){
      if(error != -ENODATA){
	log_err("Error while processing get: %s\n", strerror(-error));
      }
      goto fail;
    }
    break;
  case COMM_GET_LIST:
    if((error = process_get_list(ch, &payload)) < 0){
      if(error != -ENODATA){
	log_err("Error while processing get: %s\n", strerror(-error));
      }
      goto fail;
    }
    break;
  case COMM_SET:
    if((error = process_set(ch, payload)) < 0){
      log_err("Error while processing set: %s\n", strerror(-error));
      goto fail;
    }
    break;
  case COMM_GET_STATE:
    if((error = process_get_state(ch, &payload)) < 0){
      log_err("Error while processing get_state: %s\n", strerror(-error));
      goto fail;
    }
    break;
  case COMM_SET_STATE:
    if((error = process_set_state(ch, payload)) < 0){
      log_err("Error while processing set_state: %s\n", strerror(-error));
      goto fail;
    }
    break;
  default:
    log_err("Unknown connection request received.\n");
    error = -EINVAL;
    ch->comm_error = error;
    ch->comm_payload_size = 0;
  }

  if(ch->comm_payload_size){
    log_dbg("Reallocating transfer buffer.\n");
    tmp_ch = (comm_header_t *)
      realloc(ch,sizeof(comm_header_t)+ch->comm_payload_size);

    if(tmp_ch) { ch = tmp_ch; } else {
      log_err("Not enough memory to complete request.\n");
      error = -ENOMEM;
      goto fail;
    }
    memcpy((char *)ch+sizeof(comm_header_t), payload, ch->comm_payload_size);
  }

 fail:
  error = write(afd, ch, sizeof(comm_header_t)+ch->comm_payload_size);
  if(error < 0){
    log_sys_err("Unable to write package back to sender");
    goto fail;
  } else if(error < (sizeof(comm_header_t)+ch->comm_payload_size)){
    log_err("Unable to write complete package.\n");
    error = -EBADE;
    goto fail;
  } else {
    error = 0;
  }

  if(ch){ free(ch); }
  if(payload){ free(payload); }
  

  EXIT("process_request");
  return error;
}


/**
 * process_broadcast
 * @sfd: the UDP socket
 *
 * Returns: 0 on success, < 0 on failure
 */
int process_broadcast(int sfd){
  int error = 0;
  comm_header_t *ch = NULL;
  char *payload = NULL;
  char *buffer = NULL;
  struct sockaddr_storage addr;
  int len = sizeof(struct sockaddr_storage);  /* value/result for recvfrom */
  int discard = 0;

  ENTER("process_broadcast");

  ch = malloc(sizeof(comm_header_t));
  if(!ch){
    error = -ENOMEM;
    goto fail;
  }
  memset(ch, 0, sizeof(comm_header_t));
  memset(&addr, 0, sizeof(struct sockaddr_storage)); /* just to make sure */

  log_dbg("Waiting to receive broadcast request.\n");
  if(recvfrom(sfd, ch, sizeof(comm_header_t), 0, (struct sockaddr *)&addr, &len) < 0){
    log_sys_err("Unable to perform recvfrom");
    error = -errno;
    goto fail;
  }

  if(ch->comm_type != COMM_BROADCAST){
    /* Either someone is pinging this port, or there is an older version **
    ** of ccs trying to get bcast response.  Either way, we should not   **
    ** respond to them.................................................. */
    log_dbg("Received invalid request on broadcast port.\n");
    error = -EINVAL;
    goto fail;
  }

  /* need to ignore my own broadcasts */

  if(ch->comm_payload_size){
    /* cluster name was sent, need to read it */
  }

  if(!master_doc){
    discard = 1;
    log_dbg("master_doc not loaded.  Attempting to load it.\n");
    if(!(master_doc = malloc(sizeof(open_doc_t)))){
      error = -ENOMEM;
      goto fail;
    }
    memset(master_doc, 0, sizeof(open_doc_t));
    master_doc->od_doc = xmlParseFile("/etc/cluster/cluster.conf");
    if(!master_doc->od_doc){
      free(master_doc);
      master_doc = NULL;
      log_err("Unable to parse %s.\n", "/etc/cluster/cluster.conf");
      error = -ENODATA;
      goto fail;
    }
    log_dbg("master_doc found and loaded.\n");
  } else if(update_required){
    log_dbg("Update is required.\n");
    if((error = update_config())){
      log_err("Failed to update config file, required by cluster.\n");
      /* ATTENTION -- remove all open_doc_t's ? */
      goto fail;
    }
  }

  /* allocates space for the payload */
  xmlDocDumpFormatMemory(master_doc->od_doc,
			 (xmlChar **)&payload,
			 &(ch->comm_payload_size),
			 0);
  if(!ch->comm_payload_size){
    error = -ENOMEM;
    log_err("Document dump to memory failed.\n");
    goto fail;
  }

  buffer = malloc(ch->comm_payload_size + sizeof(comm_header_t));
  if(!buffer){
    error = -ENOMEM;
    goto fail;
  }

  if(quorate){
    ch->comm_flags |= COMM_BROADCAST_FROM_QUORATE;
  }

  memcpy(buffer, ch, sizeof(comm_header_t));
  memcpy(buffer+sizeof(comm_header_t), payload, ch->comm_payload_size);

  log_dbg("Sending cluster.conf (version %d)...\n", get_doc_version(master_doc->od_doc));
  if(sendto(sfd, buffer, ch->comm_payload_size + sizeof(comm_header_t), 0,
	    (struct sockaddr *)&addr, (socklen_t)len) < 0){
    log_sys_err("Sendto failed");
    error = -errno;
  }
  
 fail:
  if(buffer) free(buffer);
  if(payload) free(payload);
  if(ch) free(ch);
  if(discard){
    if(master_doc && master_doc->od_doc)
      xmlFreeDoc(master_doc->od_doc);
    if(master_doc) free(master_doc);
    master_doc = NULL;
  }
  EXIT("process_broadcast");
  return error;
}
