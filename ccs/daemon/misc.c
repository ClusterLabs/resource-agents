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
#include <ctype.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <libxml/parser.h>
#include <errno.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <openais/service/logsys.h>

#include "comm_headers.h"
#include "debug.h"
#include "misc.h"

volatile int quorate = 0;

int update_required = 0;
pthread_mutex_t update_lock;

open_doc_t *master_doc = NULL;

LOGSYS_DECLARE_SUBSYS ("CCS", LOG_INFO);

int get_doc_version(xmlDocPtr ldoc){
  int i;
  int error = 0;
  xmlXPathObjectPtr  obj = NULL;
  xmlXPathContextPtr ctx = NULL;
  xmlNodePtr        node = NULL;

  CCSENTER("get_doc_version");

  ctx = xmlXPathNewContext(ldoc);
  if(!ctx){
    log_printf(LOG_ERR, "Error: unable to create new XPath context.\n");
    error = -EIO;  /* ATTENTION -- what should this be? */
    goto fail;
  }

  obj = xmlXPathEvalExpression((xmlChar *)"//cluster/@config_version", ctx);
  if(!obj || !obj->nodesetval || (obj->nodesetval->nodeNr != 1)){
    log_printf(LOG_ERR, "Error while retrieving config_version.\n");
    error = -ENODATA;
    goto fail;
  }

  node = obj->nodesetval->nodeTab[0];
  if(node->type != XML_ATTRIBUTE_NODE){
    log_printf(LOG_ERR, "Object returned is not of attribute type.\n");
    error = -ENODATA;
    goto fail;
  }

  if(!node->children->content || !strlen((char *)node->children->content)){
    log_printf(LOG_DEBUG, "No content found.\n");
    error = -ENODATA;
    goto fail;
  }
  
  for(i=0; i < strlen((char *)node->children->content); i++){
    if(!isdigit(node->children->content[i])){
      log_printf(LOG_ERR, "config_version is not a valid integer.\n");
      error = -EINVAL;
      goto fail;
    }
  }
  error = atoi((char *)node->children->content);

fail:

  if(ctx){
    xmlXPathFreeContext(ctx);
  }
  if(obj){
    xmlXPathFreeObject(obj);
  }
  CCSEXIT("get_doc_version");
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
char *get_cluster_name(xmlDocPtr ldoc){
  int error = 0;
  char *rtn = NULL;
  xmlXPathObjectPtr  obj = NULL;
  xmlXPathContextPtr ctx = NULL;
  xmlNodePtr        node = NULL;

  CCSENTER("get_cluster_name");

  ctx = xmlXPathNewContext(ldoc);
  if(!ctx){
    log_printf(LOG_ERR, "Error: unable to create new XPath context.\n");
    error = -EIO;  /* ATTENTION -- what should this be? */
    goto fail;
  }

  obj = xmlXPathEvalExpression((xmlChar *)"//cluster/@name", ctx);
  if(!obj || !obj->nodesetval || (obj->nodesetval->nodeNr != 1)){
    log_printf(LOG_ERR, "Error while retrieving config_version.\n");
    error = -ENODATA;
    goto fail;
  }

  node = obj->nodesetval->nodeTab[0];
  if(node->type != XML_ATTRIBUTE_NODE){
    log_printf(LOG_ERR, "Object returned is not of attribute type.\n");
    error = -ENODATA;
    goto fail;
  }

  if(!node->children->content || !strlen((char *)node->children->content)){
    log_printf(LOG_DEBUG, "No content found.\n");
    error = -ENODATA;
    goto fail;
  }

  rtn = strdup((char *)node->children->content);

fail:

  if(ctx){
    xmlXPathFreeContext(ctx);
  }
  if(obj){
    xmlXPathFreeObject(obj);
  }
  CCSEXIT("get_cluster_name");
  return rtn;
}



