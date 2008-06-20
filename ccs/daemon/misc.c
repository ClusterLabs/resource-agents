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

/**
 * do_simple_xml_query
 * @ctx: xml context
 * @query: "/cluster/@name"
 *
 * it only handles this kind of query
 */
static char *do_simple_xml_query(xmlXPathContextPtr ctx, char *query) {
  xmlXPathObjectPtr  obj = NULL;
  xmlNodePtr        node = NULL;

  CCSENTER("do_simple_xml_query");

  obj = xmlXPathEvalExpression((xmlChar *)query, ctx);
  if(!obj || !obj->nodesetval || (obj->nodesetval->nodeNr != 1))
    log_printf(LOG_DEBUG, "Error processing query: %s.\n", query);
  else {
    node = obj->nodesetval->nodeTab[0];
    if(node->type != XML_ATTRIBUTE_NODE)
      log_printf(LOG_DEBUG, "Object returned is not of attribute type.\n");
    else {
      if(!node->children->content || !strlen((char *)node->children->content))
	log_printf(LOG_DEBUG, "No content found.\n");
      else {
        CCSEXIT("do_simple_xml_query");
	return strdup((char *)node->children->content);
      }
    }
  }

  if(obj)
    xmlXPathFreeObject(obj);

  CCSEXIT("do_simple_xml_query");
  return NULL;
}

int get_doc_version(xmlDocPtr ldoc){
  int i;
  int error = 0;
  xmlXPathContextPtr ctx = NULL;
  char *res = NULL;

  CCSENTER("get_doc_version");

  ctx = xmlXPathNewContext(ldoc);
  if(!ctx){
    log_printf(LOG_ERR, "Error: unable to create new XPath context.\n");
    error = -EIO;  /* ATTENTION -- what should this be? */
    goto fail;
  }

  res =  do_simple_xml_query(ctx, "/cluster/@config_version");
  if(res) {
    for(i=0; i < strlen(res); i++){
      if(!isdigit(res[i])){
        log_printf(LOG_ERR, "config_version is not a valid integer.\n");
        error = -EINVAL;
        goto fail;
      }
    }
    error = atoi(res);
  } else
    error = -EINVAL;

fail:

  if(res)
	free(res);

  if(ctx){
    xmlXPathFreeContext(ctx);
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
  xmlXPathContextPtr ctx = NULL;

  CCSENTER("get_cluster_name");

  ctx = xmlXPathNewContext(ldoc);
  if(!ctx){
    log_printf(LOG_ERR, "Error: unable to create new XPath context.\n");
    error = -EIO;  /* ATTENTION -- what should this be? */
    goto fail;
  }

  rtn = do_simple_xml_query(ctx, "/cluster/@name");

fail:

  if(ctx){
    xmlXPathFreeContext(ctx);
  }
  CCSEXIT("get_cluster_name");
  return rtn;
}

/**
 * set_ccs_logging
 * @ldoc:
 *
 * Returns: -1 on failure. NULL on success.
 */
int set_ccs_logging(xmlDocPtr ldoc){
  int facility = SYSLOGFACILITY, loglevel = LOG_LEVEL_INFO;
  char *res = NULL;
  xmlXPathContextPtr ctx = NULL;

  CCSENTER("set_ccs_logging");

  ctx = xmlXPathNewContext(ldoc);
  if(!ctx){
    log_printf(LOG_ERR, "Error: unable to create new XPath context.\n");
    return -1;
  }

  res = do_simple_xml_query(ctx, "/cluster/ccs/@log_facility");
  if(res) {
    facility = logsys_facility_id_get (res);
    if (facility < 0)
      facility = SYSLOGFACILITY;

    logsys_config_facility_set ("CCS", facility);
    log_printf(LOG_DEBUG, "log_facility: %s (%d).\n", res, facility);
    free(res);
    res=NULL;
  }

  res = do_simple_xml_query(ctx, "/cluster/ccs/@log_level");
  if(res) {
    loglevel = logsys_priority_id_get (res);
    if (loglevel < 0)
      loglevel = LOG_LEVEL_INFO;

    if (!debug)
      logsys_config_priority_set (loglevel);

    log_printf(LOG_DEBUG, "log_level: %s (%d).\n", res, loglevel);
    free(res);
    res=NULL;
  }

  if(ctx){
    xmlXPathFreeContext(ctx);
  }

  CCSEXIT("set_ccs_logging");
  return 0;
}
