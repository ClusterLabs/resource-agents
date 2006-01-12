/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
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
#include <unistd.h>
#include <ctype.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include "comm_headers.h"

#include "ccs.h"
#include "magma.h"
#include "magmamsg.h"

int cluster_base_port = 50008;

static int get_doc_version(xmlDocPtr ldoc){
  int i;
  int error = 0;
  xmlXPathObjectPtr  obj = NULL;
  xmlXPathContextPtr ctx = NULL;
  xmlNodePtr        node = NULL;

  ctx = xmlXPathNewContext(ldoc);
  if(!ctx){
    fprintf(stderr, "Unable to create new XPath context.\n");
    error = -EIO;  /* ATTENTION -- what should this be? */
    goto fail;
  }

  obj = xmlXPathEvalExpression("//cluster/@config_version", ctx);
  if(!obj || !obj->nodesetval || (obj->nodesetval->nodeNr != 1)){
    fprintf(stderr, "Error while retrieving config_version.\n");
    error = -ENODATA;
    goto fail;
  }

  node = obj->nodesetval->nodeTab[0];
  if(node->type != XML_ATTRIBUTE_NODE){
    fprintf(stderr, "Object returned is not of attribute type.\n");
    error = -ENODATA;
    goto fail;
  }

  if(!node->children->content || !strlen(node->children->content)){
    error = -ENODATA;
    goto fail;
  }

  for(i=0; i < strlen(node->children->content); i++){
    if(!isdigit(node->children->content[i])){
      fprintf(stderr, "config_version is not a valid integer.\n");
      error = -EINVAL;
      goto fail;
    }
  }
  error = atoi(node->children->content);

 fail:
  if(ctx){
    xmlXPathFreeContext(ctx);
  }
  if(obj){
    xmlXPathFreeObject(obj);
  }
  return error;
}

int update(char *location){
  int error = 0;
  int i, fd;
  int cluster_fd = -1;
  char true_location[256];
  xmlDocPtr doc = NULL;
  char *mem_doc = NULL;
  int doc_size = 0;
  char *buffer = NULL;
  comm_header_t *ch = NULL, rch;
  cluster_member_list_t *membership = NULL;
  int desc;
  char *v1_str,*v3_str;
  int v1, v2, v3;

  if(location[0] != '/'){
    memset(true_location, 0, 256);
    if(!getcwd(true_location, 256)){
      fprintf(stderr, "Unable to get the current working directory.\n");
      return -errno;
    }
    true_location[strlen(true_location)] = '/';
    strncpy(true_location+strlen(true_location), location, 256-strlen(true_location));
  } else {
    strncpy(true_location, location, 256);
  }

 desc = ccs_connect();
  if(desc < 0){
    fprintf(stderr, "Unable to connect to the CCS daemon: %s\n", strerror(-desc));
    return desc;
  }

  if((error = ccs_get(desc, "//@config_version", &v1_str))){
    fprintf(stderr, "Unable to get current config_version: %s\n", strerror(-error));
    ccs_disconnect(desc);
    return error;
  }
  ccs_disconnect(desc);

  for(i=0; i < strlen(v1_str); i++){
    if(!isdigit(v1_str[i])){
      fprintf(stderr, "config_version is not a valid integer.\n");
      free(v1_str);
      return -EINVAL;
    }
  }
  v1 = atoi(v1_str);
  free(v1_str);

  doc = xmlParseFile(true_location);
  if(!doc){
    fprintf(stderr, "Unable to parse %s\n", true_location);
    return -EINVAL;
  }
  v2 = get_doc_version(doc);
  if(v2 < 0){
    fprintf(stderr, "Unable to get the config_version from %s\n",
	    location);
    xmlFreeDoc(doc);
    return -EINVAL;
  }
  if(v2 <= v1){
    fprintf(stderr,
	    "Proposed updated config file does not have greater version number.\n"
	    "  Current config_version :: %d\n"
	    "  Proposed config_version:: %d\n", v1, v2);
    xmlFreeDoc(doc);
    return -EINVAL;
  }    

  xmlDocDumpFormatMemory(doc, (xmlChar **)&mem_doc, &doc_size, 0);
  if(!mem_doc){
    fprintf(stderr, "Unable to allocate memory for update document.\n");
    xmlFreeDoc(doc);
    return -ENOMEM;
  }
  xmlFreeDoc(doc);

  buffer = malloc(doc_size + sizeof(comm_header_t));
  if(!buffer){
    fprintf(stderr, "Unable to allocate memory for transfer buffer.\n");
    free(mem_doc);
    return -ENOMEM;
  }
  memset(buffer, 0, (doc_size + sizeof(comm_header_t)));
  ch = (comm_header_t *)buffer;
  memcpy(buffer+sizeof(comm_header_t), mem_doc, doc_size);
  free(mem_doc);

  ch->comm_type = COMM_UPDATE;
  ch->comm_flags= COMM_UPDATE_NOTICE;
  ch->comm_payload_size = doc_size;

  cluster_fd = clu_connect(NULL, 0);
  if(cluster_fd < 0){
    fprintf(stderr, "Unable to connect to cluster infrastructure.\n");
    if((errno == ENOENT) || (errno == ELIBACC)){
      fprintf(stderr, "Hint:  The magma plugins can not be found.\n");
    }
    
    return cluster_fd;
  }

  if(!(clu_quorum_status(NULL) & QF_QUORATE)){
    fprintf(stderr, "Unable to honor update request.  Cluster is not quorate.\n");
    return -EPERM;
  }
  
  membership = clu_member_list(NULL);
  msg_update(membership);
  memb_resolve_list(membership, NULL);


  swab_header(ch);
  
  for(i=0; i < membership->cml_count; i++){
    fd = msg_open(membership->cml_members[i].cm_id, cluster_base_port, 0, 5);

    if(fd < 0){
      fprintf(stderr, "Unable to open connection to %s: %s\n",
	       membership->cml_members[i].cm_name, strerror(errno));
      free(buffer);
      cml_free(membership);
      return -errno;
    }

    error = msg_send(fd, buffer, sizeof(comm_header_t) + doc_size);
    if(error < 0){
      fprintf(stderr, "Unable to send msg to %s: %s\n",
	      membership->cml_members[i].cm_name, strerror(errno));
      msg_close(fd);
      free(buffer);
      cml_free(membership);
      return -errno;
    }

    error = msg_receive_timeout(fd, &rch, sizeof(comm_header_t), 5);
    swab_header(&rch);
    if(error < 0){
      fprintf(stderr, "Failed to receive COMM_UPDATE_NOTICE_ACK from %s.\n",
	      membership->cml_members[i].cm_name);
      fprintf(stderr, "Hint: Check the log on %s for reason.\n",
	      membership->cml_members[i].cm_name);
      msg_close(fd);
      free(buffer);
      cml_free(membership);
      return -errno;
    }
    msg_close(fd);
  }

  swab_header(ch);
  
  ch->comm_flags = COMM_UPDATE_COMMIT;

  swab_header(ch);

  for(i=0; i < membership->cml_count; i++){
    fd = msg_open(membership->cml_members[i].cm_id, cluster_base_port, 0, 5);
    if(fd < 0){
      fprintf(stderr, "Unable to open connection to %s: %s\n",
	       membership->cml_members[i].cm_name, strerror(errno));
      free(buffer);
      cml_free(membership);
      return -errno;
    }
    error = msg_send(fd, buffer, sizeof(comm_header_t));
    if(error < 0){
      fprintf(stderr, "Unable to send msg to %s: %s\n",
	      membership->cml_members[i].cm_name, strerror(errno));
      msg_close(fd);
      free(buffer);
      cml_free(membership);
      return -errno;
    }
    error = msg_receive_timeout(fd, &rch, sizeof(comm_header_t), 5);
    swab_header(&rch);
    if(error < 0){
      fprintf(stderr, "Failed to receive COMM_UPDATE_COMMIT_ACK from %s.\n",
	      membership->cml_members[i].cm_name);
      fprintf(stderr, "Hint: Check the log on %s for reason.\n",
	      membership->cml_members[i].cm_name);
      msg_close(fd);
      free(buffer);
      cml_free(membership);
      return -errno;
    }
    msg_close(fd);
    error = 0;
  }

  free(buffer);
  cml_free(membership);

  /* If we can't connect here, it doesn't mean the update failed **
  ** It means that we simply can't report the change in version  */
  desc = ccs_connect();
  if(desc < 0){
    fprintf(stderr, "Unable to connect to the CCS daemon: %s\n", strerror(-desc));
    return 0;
  }

  if((error = ccs_get(desc, "//@config_version", &v3_str))){
    ccs_disconnect(desc);
    return 0;
  }
  v3 = atoi(v3_str);
  free(v3_str);
  ccs_disconnect(desc);

  if(v2 == v3){
    printf("Config file updated from version %d to %d\n", v1, v2);
  } else {
    fprintf(stderr, "Warning:: Simultaneous update requests detected.\n"
	    "  You have lost the race.\n"
	    "  Old config version :: %d\n"
	    "  Proposed config version :: %d\n"
	    "  Winning config version  :: %d\n\n"
	    "Check /etc/cluster/cluster.conf to ensure it contains the desired contents.\n", v1, v2, v3);
    return -EAGAIN;
  }
  return 0;
}
