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
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include "ccs.h"

int update(char *location){
  int error = 0;
  char true_location[256];
  char *rtn_str = NULL;
  xmlDocPtr doc = NULL;

  if(location[0] != '/'){
    memset(true_location, 0, 256);
    if(!getcwd(true_location, 256)){
      fprintf(stderr, "Unable to get the current working directory.\n");
      return -errno;
    }
    true_location[strlen(true_location)] = '/';
    strncpy(true_location+strlen(true_location), location, 256-strlen(true_location));
  }

  doc = xmlParseFile(true_location);
  if(!doc){
    fprintf(stderr, "Unable to parse %s\n", true_location);
    return -EINVAL;
  }
  xmlFreeDoc(doc);

  error = ccs_update(true_location, &rtn_str);

  if(error && !rtn_str){
    /* most likely a communication error, since no reason is given by server */
    fprintf(stderr, "Update failed: %s\n", strerror(-error));
  }
  if(rtn_str){
    fprintf((error)?stderr:stdout, "%s", rtn_str);
    free(rtn_str);
  }
  return error;
}
