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
#include "cman_mgr.h"

#define MAX_OPEN_CONNECTIONS 10
typedef struct open_connection_s {
  char *oc_cwp;
  char *oc_query;
  int oc_index;
  xmlXPathContextPtr oc_ctx;
} open_connection_t;

/* ATTENTION: need to lock on this if we start forking the daemon **
**  Also would need to create a shared memory area for open cnx's */
static open_connection_t *ocs[MAX_OPEN_CONNECTIONS];

static xmlDocPtr doc = NULL;  /* master copy of cluster.xml */

/* ATTENTION -- does this need to be atomic? */
int update_in_progress = 0;


static int get_doc_version(xmlDocPtr ldoc){
  int error = 0;
  xmlXPathObjectPtr  obj = NULL;
  xmlXPathContextPtr ctx = NULL;
  xmlNodePtr        node = NULL;

  ENTER("get_doc_version");

  ctx = xmlXPathNewContext(ldoc);
  if(!ctx){
    log_err("Error: unable to create new XPath context.\n");
    error = -EIO;  /* ATTENTION -- what should this be? */
    goto fail;
  }

  obj = xmlXPathEvalExpression("//cluster/@config_version", ctx);
  if(!obj || !obj->nodesetval || (obj->nodesetval->nodeNr != 1)){
    log_err("Error while retrieving config_version.\n");
    error = -EIO;
    goto fail;
  }

  node = obj->nodesetval->nodeTab[0];
  if(node->type != XML_ATTRIBUTE_NODE){
    log_err("Object returned is not of attribute type.\n");
    error = -EIO;
    goto fail;
  }

  if(!node->children->content || !strlen(node->children->content)){
    log_dbg("No content found.\n");
    error = -ENODATA;
    goto fail;
  }

  error = atoi(node->children->content);

 fail:
  if(ctx){
    xmlXPathFreeContext(ctx);
  }
  if(obj){
    xmlXPathFreeObject(obj);
  }
  EXIT("get_doc_version");
  return error;
}


/**
 * get_cluster_name
 * @ldoc:
 *
 * The caller must remember to free the string that is returned.
 *
 * Returns: NULL on failure, (char *) otherwise
 */
static char *get_cluster_name(xmlDocPtr ldoc){
  int error = 0;
  xmlXPathObjectPtr  obj = NULL;
  xmlXPathContextPtr ctx = NULL;
  xmlNodePtr        node = NULL;

  ENTER("get_cluster_name");

  ctx = xmlXPathNewContext(ldoc);
  if(!ctx){
    log_err("Error: unable to create new XPath context.\n");
    error = -EIO;  /* ATTENTION -- what should this be? */
    goto fail;
  }

  obj = xmlXPathEvalExpression("//cluster/@name", ctx);
  if(!obj || !obj->nodesetval || (obj->nodesetval->nodeNr != 1)){
    log_err("Error while retrieving config_version.\n");
    error = -EIO;
    goto fail;
  }

  node = obj->nodesetval->nodeTab[0];
  if(node->type != XML_ATTRIBUTE_NODE){
    log_err("Object returned is not of attribute type.\n");
    error = -EIO;
    goto fail;
  }

  if(!node->children->content || !strlen(node->children->content)){
    log_dbg("No content found.\n");
    error = -ENODATA;
    goto fail;
  }

  EXIT("get_cluster_name");
  return strdup(node->children->content);

 fail:
  if(ctx){
    xmlXPathFreeContext(ctx);
  }
  if(obj){
    xmlXPathFreeObject(obj);
  }
  EXIT("get_cluster_name");
  return NULL;
}


static void update_thread(int *do_remote){
  int remote = *do_remote;
  int connections_exist;
  int do_update = 0;
  int i;
  int v1=0, v2=0;
  xmlDocPtr tmp_doc = NULL;
  char *mem_doc = NULL;
  int mem_doc_size;

  ENTER("update_thread");

  tmp_doc = xmlParseFile("/etc/cluster/cluster.xml");
  if(!tmp_doc){
    log_err("Unable to parse %s\n", "/etc/cluster/cluster.xml");
  } else if(get_doc_version(tmp_doc) < 0){
    log_err("Unable to get config_version from cluster.xml.\n");
    xmlFreeDoc(tmp_doc);
  } else {
    v1 = get_doc_version(doc);
    v2 = get_doc_version(tmp_doc);
    if(v1 < v2){
      /* ATTENTION -- there must be no connections, otherwise context gets screwed */
      do_update = 1;
    } else {
      log_err("Cluster.xml on-disk version is <= to in-memory version.\n");
      log_err(" On-disk version   : %d\n", v2);
      log_err(" In-memory version : %d\n", v1);
      xmlFreeDoc(tmp_doc);
    }
  }

  if(do_update){
    log_msg("Updating in-memory cluster.xml (version %d => %d).\n", v1, v2);
    while(1){
      connections_exist = 0;
      for(i=0; i < MAX_OPEN_CONNECTIONS; i++){
	if(ocs[i]){
	  connections_exist = 1;
	}
      }
      if(!connections_exist){
	break;
      }
      log_dbg("Waiting for connections to drop before doing update.\n");
      sleep(1);
    }

    if(remote){
      log_dbg("remote is set (%d)\n", remote);
      xmlDocDumpFormatMemory(tmp_doc, (xmlChar **)&mem_doc, &mem_doc_size, 0);
      if(!mem_doc_size || !mem_doc){
	log_err("Unable to dump document to memory.\n");
	log_err("  mem_doc_size = %d\n", mem_doc_size);
	xmlFreeDoc(tmp_doc);
	return;
      }

      if(update_remote_nodes(mem_doc, mem_doc_size)){
	xmlFreeDoc(tmp_doc);
	log_err("Failed to update remote nodes.\n");
      } else {
	xmlFreeDoc(doc);
	doc = tmp_doc;
	log_msg("Update complete.\n");
      }
      if(mem_doc) { free(mem_doc); }
    } else {
      log_dbg("remote is unset (%d)\n", remote);
      xmlFreeDoc(doc);
      doc = tmp_doc;
      log_msg("Update complete.\n");
    }
  } else {
    log_msg("Update failed.\n");
  }

  update_in_progress = 0;
  EXIT("update_thread");
  return;
}


static void update_handler(int sig){
  int do_remote = 1;
  pthread_t	thread;

  ENTER("update_handler");

  if(sig != SIGHUP){
    log_err("Inappropriate signal received.  Ignoring.\n");
    goto out;
  }

  if(!quorate){
    log_err("Unable to honor update request.  Cluster is not quorate.\n");
    goto fail;
  }

  if(update_in_progress){
    log_err("An update is already in progress.\n");
    goto out;
  }
  update_in_progress = 1;

  if(pthread_create(&thread, NULL, (void *)update_thread, (void *)&do_remote)){
    log_err("Failed to create update thread.\n");
    goto fail;
  }

 out:
  EXIT("update_handler");
  return;

 fail:
  log_err("Update failed.\n");
  EXIT("update_handler");
  return;
}


/**
 * broadcast_for_doc
 *
 * Returns: 0 on success, < 0 on error
 */
static int broadcast_for_doc(char *cluster_name, int blocking){
  int error = 0;
  int sfd = -1;
  int trueint;
  int intlen;
  int v1, v2;
  int write_to_disk = 0;
  char *tmp_name = NULL;
  struct sockaddr_in addr;
  int len=sizeof(struct sockaddr_in);
  //  char *bcast_addr = "192.168.47.255";
  comm_header_t *ch = NULL;
  char *bdoc = NULL;
  fd_set rset;
  struct timeval tv;
  static xmlDocPtr tmp_doc = NULL;

  ENTER("broadcast_for_doc");

  ch = malloc(sizeof(comm_header_t));
  if(!ch){
    error = -ENOMEM;
    goto fail;
  }
  memset(ch, 0, sizeof(comm_header_t));

  sfd = socket(PF_INET, SOCK_DGRAM, 0);
  if(sfd < 0){
    log_sys_err("Unable to create socket for broadcast");
    error = -errno;
    goto fail;
  }

  getsockopt(sfd, SOL_SOCKET, SO_BROADCAST, &trueint, &intlen);
  if(trueint){
    log_dbg("Broadcast is allowed.\n");
  } else {
    log_dbg("Broadcast is not allowed.\n");
    trueint = 1;
    if((error = setsockopt(sfd, SOL_SOCKET, SO_BROADCAST, &trueint, sizeof(int)))){
      log_err("Unable to set socket options: %s\n", strerror(-error));
      goto fail;
    } else {
      log_dbg("  Broadcast enabled.\n");
    }
  }

  FD_ZERO(&rset);

  do {
    addr.sin_family = AF_INET;
    /* While INADDR_BROADCAST is better than hardcoding, it may not be the best
    log_msg("Broadcasting on %s\n", bcast_addr);
    inet_aton(bcast_addr, (struct in_addr *)&addr.sin_addr.s_addr);
    */
    addr.sin_addr.s_addr = INADDR_BROADCAST;
    /* ATTENTION -- fix hardcoded values */
    addr.sin_port = htons(50007);

    ch->comm_type = COMM_BROADCAST;

    log_dbg("Sending broadcast.\n");
    sendto(sfd, (char *)ch, sizeof(comm_header_t), 0,
	   (struct sockaddr *)&addr, (socklen_t)len);

    FD_SET(sfd, &rset);
    tv.tv_sec = 0;
    tv.tv_usec = 250000;
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
		 (struct sockaddr *)&addr, (socklen_t *)&len);
	if(!ch->comm_payload_size || ch->comm_error){
	  /* clean out this reply by not using MSG_PEEK */
	  recvfrom(sfd, (char *)ch, sizeof(comm_header_t), 0,
		   (struct sockaddr *)&addr, (socklen_t *)&len);
	  error = -ENODATA;
	  FD_SET(sfd, &rset);
	  continue;
	}
	bdoc = malloc(ch->comm_payload_size + sizeof(comm_header_t));
	if(!bdoc){
	  error = -ENOMEM;
	  goto fail;
	}
	memset(bdoc, 0, ch->comm_payload_size + sizeof(comm_header_t));
	/* ATTENTION -- potential for incomplete package */
	recvfrom(sfd, bdoc, ch->comm_payload_size + sizeof(comm_header_t),
		 0, (struct sockaddr *)&addr, &len);
	tmp_doc = xmlParseMemory(bdoc+sizeof(comm_header_t), ch->comm_payload_size);
	if(!tmp_doc){
	  log_err("Unable to parse remote cluster.xml.\n");
	  free(bdoc); bdoc = NULL;
	  continue;
	}
	tmp_name = get_cluster_name(tmp_doc);
	log_dbg("  Given cluster name = %s\n", cluster_name);
	log_dbg("  Remote cluster name= %s\n", tmp_name);
	if(!tmp_name){
	  log_err("Unable to find cluster name in remote cluster.xml.\n");
	  free(bdoc); bdoc = NULL;
	  xmlFreeDoc(tmp_doc); tmp_doc = NULL;
	  continue;
	} else if(cluster_name && strcmp(cluster_name, tmp_name)){
	  log_dbg("Remote and local cluster.xml have different cluster names.\n");
	  log_dbg("Skipping...\n");
	  free(tmp_name); tmp_name = NULL;
	  free(bdoc); bdoc = NULL;
	  xmlFreeDoc(tmp_doc); tmp_doc = NULL;
	  continue;
	}
	free(tmp_name); tmp_name = NULL;
	if(!doc){
	  if((v2 = get_doc_version(tmp_doc)) >= 0){
	    log_msg("Remote copy of cluster.xml (version = %d) found.\n", v2);
	    doc = tmp_doc;
	    tmp_doc = NULL;
	    write_to_disk = 1;
	  }
	} else {
	  if(((v1 = get_doc_version(doc)) >= 0) &&
	     ((v2 = get_doc_version(tmp_doc)) >= 0)){
	    if(ch->comm_flags & COMM_BROADCAST_FROM_QUORATE){
	      log_msg("Remote copy of cluster.xml is from quorate node.\n");
	      log_msg(" Local version # : %d\n", v1);
	      log_msg(" Remote version #: %d\n", v2);
	      if(v1 != v2){
		log_msg("Switching to remote copy.\n");
	      }
	      xmlFreeDoc(doc);
	      doc = tmp_doc;
	      tmp_doc = NULL;
	      write_to_disk = 1;
	      goto out;
	    } else if(v2 > v1){
	      log_msg("Remote copy of cluster.xml is newer than local copy.\n");
	      log_msg(" Local version # : %d\n", v1);
	      log_msg(" Remote version #: %d\n", v2);
	      xmlFreeDoc(doc);
	      doc = tmp_doc;
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
      tv.tv_sec = 0;
      tv.tv_usec = 250000; /* 1/4 of a second */
    }
  } while(blocking && !doc);
 out:
  if(error){
    goto fail;
  }

  if(write_to_disk){
    struct stat stat_buf;
    FILE *f;
    /* We did not have a copy available, so write it out */

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
    f = fopen("/etc/cluster/cluster.xml", "w");
    if(!f){
      log_sys_err("Unable to open /etc/cluster/cluster.xml");
      error = -errno;
      goto fail;
    }
    if(xmlDocDump(f, doc) < 0){
      error = -EIO;
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
  char *tmp_name = NULL;

  ENTER("process_connect");

  ch->comm_payload_size = 0;

  log_dbg("Given cluster name is = %s\n", cluster_name);

  if(!quorate){
    if(!(ch->comm_flags & COMM_CONNECT_FORCE)){
      log_msg("Cluster is not quorate.  Refusing connection.\n");
      error = -ECONNREFUSED;
      goto fail;
    }

    if(!doc){
      memset(ocs, 0, sizeof(open_connection_t *)*MAX_OPEN_CONNECTIONS);
      doc = xmlParseFile("/etc/cluster/cluster.xml");
      if(!doc){
	log_msg("Unable to parse %s\n", "/etc/cluster/cluster.xml");
	log_msg("Searching cluster for valid copy.\n");
	/* Not a problem, can get it from broadcast */
      } else if((error = get_doc_version(doc)) < 0){
	log_err("Unable to get config_version from cluster.xml.\n");
	log_err("Discarding data and searching for valid copy.\n");
	xmlFreeDoc(doc);
	doc = NULL;
      } else {
	tmp_name = get_cluster_name(doc);
	if(!tmp_name){
	  log_err("Unable to get cluster name from cluster.xml.\n");
	  log_err("Discarding data and searching for valid copy.\n");
	  xmlFreeDoc(doc);
	  doc = NULL;
	} else if(cluster_name && strcmp(cluster_name, tmp_name)){
	  log_err("Given cluster name does not match local cluster.xml.\n");
	  log_err("Discarding data and searching for matching copy.\n");
	  xmlFreeDoc(doc);
	  doc = NULL;
	  free(tmp_name); tmp_name = NULL;
	} else {
	  log_msg("cluster.xml (cluster name = %s, version = %d) found.\n",
		  tmp_name, error);
	}
      }
      error = 0;
    } else {
      tmp_name = get_cluster_name(doc);
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

    /* Need to broadcast regardless (unless connected to cman) to check version # */
    if((error = broadcast_for_doc(tmp_name, ch->comm_flags & COMM_CONNECT_BLOCKING)) && !doc){
      log_err("Broadcast for config file failed: %s\n", strerror(-error));
      goto fail;
    }
    error = 0;
  }

  if(!doc){
    memset(ocs, 0, sizeof(open_connection_t *)*MAX_OPEN_CONNECTIONS);
    log_err("The appropriate config file could not be loaded.\n");
    error = -ENODATA;
    goto fail;
  }

  /* now that we have a document, set the update handler */
  /* ATTENTION -- does it hurt to set this more than once? */
  signal(SIGHUP, &update_handler);

  if(update_config){
    pthread_t thread;
    int do_remote = 0;

    if(!update_in_progress){
      update_in_progress = 1;
      log_dbg("cluster.xml updated.  Rereading config file.\n");
      if(pthread_create(&thread, NULL, (void *)update_thread, (void *)&do_remote)){
	log_err("Failed to create update thread.\n");
	goto fail;
      }
      sleep(1); /* give the thread a chance to update */
      update_config = 0;
    }
  }
  if(update_in_progress){
    log_dbg("An update is in progress, refusing connection.\n");
    error = -EAGAIN;
    goto fail;
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
    /* try sending -ENOMEM to requestor */
    error = -ENOMEM;
    goto fail;
  }

  memset(ocs[i], 0, sizeof(open_connection_t));

  ocs[i]->oc_ctx = xmlXPathNewContext(doc);
  if(!ocs[i]->oc_ctx){
    free(ocs[i]);
    log_err("Error: unable to create new XPath context.\n");
    error = -EIO;  /* ATTENTION -- what should this be? */
    goto fail;
  }

  /* return desc to requestor */
  
 fail:
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

  if(!ocs[desc]){
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


static int process_get(comm_header_t *ch, char **payload){
  int error = 0;
  xmlXPathObjectPtr obj = NULL;
  char *query = NULL;

  ENTER("process_get");
  if(!ch->comm_payload_size){
    log_err("process_get: payload size is zero.\n");
    error = -EINVAL;
    goto fail;
  }

  if(!ocs[ch->comm_desc]){
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
    free(ocs[ch->comm_desc]->oc_query);
    ocs[ch->comm_desc]->oc_query = (char *)strdup(*payload);
  }

  /* ATTENTION -- should path expansion go before index inc ? */
  if(((ch->comm_payload_size > 2) &&
      ((*payload)[0] == '/') &&
      ((*payload)[1] == '/')) ||
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
      if((obj->nodesetval->nodeNr == 1) || 
	 (ocs[ch->comm_desc]->oc_index < obj->nodesetval->nodeNr)){
	xmlNodePtr node;
	int size=0;
	int nnv=0; /* name 'n' value */

	log_dbg("Using %s\n",(obj->nodesetval->nodeNr > 1) ? "index": "zero");
	node = obj->nodesetval->nodeTab[(obj->nodesetval->nodeNr > 1) ?
					ocs[ch->comm_desc]->oc_index :
	                                0];
	
	log_dbg("Node (%s) type = %d (%s)\n", node->name, node->type,
		(node->type == 1)? "XML_ELEMENT_NODE":
		(node->type == 2)? "XML_ATTRIBUTE_NODE":"");

	if(!node->children->content || !strlen(node->children->content)){
	  log_dbg("No content found.\n");
	  error = -ENODATA;
	  goto fail;
	}

	if((node->type == XML_ATTRIBUTE_NODE) && strstr(query, "@*")){
	  /* add on the trailing NULL and the '=' separator */
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
	log_dbg("Index reset (end of list).\n");
	ch->comm_payload_size = 0;
	ocs[ch->comm_desc]->oc_index = -1;  /* reset index after end of list */
      }
    } else {
      log_msg("No nodes found.\n");
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
  if(error){
    ch->comm_error = error;
    ch->comm_payload_size = 0;
    if(query) { free(query); }
  }
  EXIT("process_get");
  return error;
}


static int process_set(comm_header_t *ch, char *payload){
  int error = 0;

  ENTER("process_set");
  if(!ch->comm_payload_size){
    log_err("process_set: payload size is zero.\n");
    error = -EINVAL;
    goto fail;
  }

  if(!ocs[ch->comm_desc]){
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

  if(!ocs[ch->comm_desc]){
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

  if(!ocs[ch->comm_desc]){
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
 * process_connections
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
      if(error == ENODATA){
	log_msg("Error while processing get: %s\n", strerror(-error));
      } else {
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
 * @afd: the accepted socket
 *
 * Returns: 0 on success, < 0 on failure
 */
int process_broadcast(int sfd){
  int error = 0;
  comm_header_t *ch = NULL;
  char *payload = NULL;
  char *buffer = NULL;
  struct sockaddr_in addr;
  int len;

  ENTER("process_broadcast");

  ch = malloc(sizeof(comm_header_t));
  if(!ch){
    error = -ENOMEM;
    goto fail;
  }
  memset(ch, 0, sizeof(comm_header_t));
  recvfrom(sfd, ch, sizeof(comm_header_t), 0, (struct sockaddr *)&addr, &len);

  if(ch->comm_type != COMM_BROADCAST){
    log_err("Recieved invalid request on broadcast port.\n");
    error = -EINVAL;
    goto fail;
  }

  /* need to ignore my own broadcasts */

  if(ch->comm_payload_size){
    /* cluster name was sent, need to read it */
  }

  if(!doc){
    doc = xmlParseFile("/etc/cluster/cluster.xml");
    if(!doc){
      log_err("Unable to parse %s: %s\n", "/etc/cluster/cluster.xml", strerror(errno));
      error = -ENODATA;
      goto fail;
    }
  }

  /* allocates space for the payload */
  xmlDocDumpFormatMemory(doc, (xmlChar **)&payload, &(ch->comm_payload_size), 0);
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

  sendto(sfd, buffer, ch->comm_payload_size + sizeof(comm_header_t), 0,
	 (struct sockaddr *)&addr, (socklen_t)len);
  
 fail:
  if(buffer) free(buffer);
  if(payload) free(payload);
  if(ch) free(ch);

  EXIT("process_broadcast");
  return error;
}
