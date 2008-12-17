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
#include <limits.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#define SYSLOG_NAMES
#include <syslog.h>
#include <liblogthread.h>

#include "comm_headers.h"
#include "debug.h"
#include "misc.h"

volatile int quorate = 0;

int update_required = 0;
pthread_mutex_t update_lock;

open_doc_t *master_doc = NULL;

extern int nodaemon;

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
    logt_print(LOG_DEBUG, "Error processing query: %s.\n", query);
  else {
    node = obj->nodesetval->nodeTab[0];
    if(node->type != XML_ATTRIBUTE_NODE)
      logt_print(LOG_DEBUG, "Object returned is not of attribute type.\n");
    else {
      if(!node->children->content || !strlen((char *)node->children->content))
	logt_print(LOG_DEBUG, "No content found.\n");
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
    logt_print(LOG_ERR, "Error: unable to create new XPath context.\n");
    error = -EIO;  /* ATTENTION -- what should this be? */
    goto fail;
  }

  res =  do_simple_xml_query(ctx, "/cluster/@config_version");
  if(res) {
    for(i=0; i < strlen(res); i++){
      if(!isdigit(res[i])){
        logt_print(LOG_ERR, "config_version is not a valid integer.\n");
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
    logt_print(LOG_ERR, "Error: unable to create new XPath context.\n");
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

static int facility_id_get(char *name)
{
  unsigned int i;

  for (i = 0; facilitynames[i].c_name != NULL; i++) {
    if (strcasecmp(name, facilitynames[i].c_name) == 0) {
      return (facilitynames[i].c_val);
    }
  }
  return (-1);
}

static int priority_id_get(char *name)
{
  unsigned int i;

  for (i = 0; prioritynames[i].c_name != NULL; i++) {
    if (strcasecmp(name, prioritynames[i].c_name) == 0) {
      return (prioritynames[i].c_val);
    }
  }
  return (-1);
}

/**
 * set_ccs_logging
 * @ldoc:
 *
 * Returns: -1 on failure. NULL on success.
 */
int set_ccs_logging(xmlDocPtr ldoc, int reconf){
  int mode = LOG_MODE_OUTPUT_FILE | LOG_MODE_OUTPUT_SYSLOG;
  int syslog_facility = SYSLOGFACILITY;
  int syslog_priority = SYSLOGLEVEL;
  char logfile[PATH_MAX];
  int logfile_priority = SYSLOGLEVEL;
  int val;
  char *res = NULL;
  xmlXPathContextPtr ctx = NULL;

  CCSENTER("set_ccs_logging");

  /* defaults */
  memset(logfile, 0, PATH_MAX);
  sprintf(logfile, LOGDIR "/ccs.log");

  if(nodaemon)
    mode |= LOG_MODE_OUTPUT_STDERR;

  if(!reconf) /* init defaults here */
    logt_init("CCS", mode, syslog_facility, syslog_priority, logfile_priority, logfile);

  if(!ldoc) {
    CCSEXIT("set_ccs_logging (default settings)");
    return 0;
  }

  if(!ldoc && reconf) {
    CCSEXIT("set_ccs_logging (reconf with no doc?)");
    return -1;
  }

  ctx = xmlXPathNewContext(ldoc);
  if(!ctx){
    logt_print(LOG_ERR, "Error: unable to create new XPath context.\n");
    return -1;
  }

  /** This clone the same stuff in ccs_read_logging
   ** that unfortunately we cannot use in ccsd itself..
   **/

  /* to_syslog */
  res = do_simple_xml_query(ctx, "/cluster/logging/@to_syslog");
  if(res) {
    if(!strcmp(res, "yes"))
      mode |= LOG_MODE_OUTPUT_SYSLOG;
    else if(!strcmp(res, "no"))
      mode &= ~LOG_MODE_OUTPUT_SYSLOG;

    free(res);
    res=NULL;
  }

  res = do_simple_xml_query(ctx, "/cluster/logging/logging_subsys[@subsys=\"CCS\"]/@to_syslog");
  if(res) {
    if(!strcmp(res, "yes"))
      mode |= LOG_MODE_OUTPUT_SYSLOG;
    else if(!strcmp(res, "no"))
      mode &= ~LOG_MODE_OUTPUT_SYSLOG;

    free(res);
    res=NULL;
  }

  /* to logfile */
  res = do_simple_xml_query(ctx, "/cluster/logging/@to_logfile");
  if(res) {
    if(!strcmp(res, "yes"))
      mode |= LOG_MODE_OUTPUT_FILE;
    else if(!strcmp(res, "no"))
      mode &= ~LOG_MODE_OUTPUT_FILE;

    free(res);
    res=NULL;
  }

  res = do_simple_xml_query(ctx, "/cluster/logging/logging_subsys[@subsys=\"CCS\"]/@to_logfile");
  if(res) {
    if(!strcmp(res, "yes"))
      mode |= LOG_MODE_OUTPUT_FILE;
    else if(!strcmp(res, "no"))
      mode &= ~LOG_MODE_OUTPUT_FILE;

    free(res);
    res=NULL;
  }

  /* syslog_facility */
  res = do_simple_xml_query(ctx, "/cluster/logging/@syslog_facility");
  if(res) {
    val = facility_id_get(res);
    if (val >= 0)
      syslog_facility = val;

    free(res);
    res=NULL;
  }

  res = do_simple_xml_query(ctx, "/cluster/logging/logging_subsys[@subsys=\"CCS\"]/@syslog_facility");
  if(res) {
    val = facility_id_get(res);
    if (val >= 0)
      syslog_facility = val;

    free(res);
    res=NULL;
  }

  /* syslog_priority */
  res = do_simple_xml_query(ctx, "/cluster/logging/@syslog_priority");
  if(res) {
    val = priority_id_get(res);
    if (val >= 0)
      syslog_priority = val;

    free(res);
    res=NULL;
  }

  res = do_simple_xml_query(ctx, "/cluster/logging/logging_subsys[@subsys=\"CCS\"]/@syslog_priority");
  if(res) {
    val = priority_id_get(res);
    if (val >= 0)
      syslog_priority = val;

    free(res);
    res=NULL;
  }

  /* logfile */
  res = do_simple_xml_query(ctx, "/cluster/logging/@logfile");
  if(res) {
    memset(logfile, 0, PATH_MAX);
    strcpy(logfile, res);

    free(res);
    res=NULL;
  }

  res = do_simple_xml_query(ctx, "/cluster/logging/logging_subsys[@subsys=\"CCS\"]/@logfile");
  if(res) {
    memset(logfile, 0, PATH_MAX);
    strcpy(logfile, res);

    free(res);
    res=NULL;
  }

  if(debug) {
    logfile_priority = LOG_DEBUG;
    goto debug_out;
  }

  /* debug */
  res = do_simple_xml_query(ctx, "/cluster/logging/@debug");
  if(res) {
    if(!strcmp(res, "on"))
      debug = 1;

    free(res);
    res=NULL;
  }

  res = do_simple_xml_query(ctx, "/cluster/logging/logging_subsys[@subsys=\"CCS\"]/@debug");
  if(res) {
    if(!strcmp(res, "on"))
      debug = 1;
    else if(!strcmp(res, "off"))
      debug = 0;

    free(res);
    res=NULL;
  }

  if (debug) {
    logfile_priority = LOG_DEBUG; 
    goto debug_out;
  }

  /* logfile_priority */
  res = do_simple_xml_query(ctx, "/cluster/logging/@logfile_priority");
  if(res) {
    val = priority_id_get(res);
    if (val >= 0)
      syslog_priority = val;

    free(res);
    res=NULL;
  }

  res = do_simple_xml_query(ctx, "/cluster/logging/logging_subsys[@subsys=\"CCS\"]/@logfile_priority");
  if(res) {
    val = priority_id_get(res);
    if (val >= 0)
      syslog_priority = val;

    free(res);
    res=NULL;
  }

debug_out:
  if(ctx){
    xmlXPathFreeContext(ctx);
  }

  logt_conf("CCS", mode, syslog_facility, syslog_priority, logfile_priority, logfile);

  CCSEXIT("set_ccs_logging");
  return 0;
}
