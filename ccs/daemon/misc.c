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
  int facility = SYSLOGFACILITY, loglevel = LOG_LEVEL_INFO, global_debug = 0;
  char *res = NULL, *error = NULL;
  xmlXPathContextPtr ctx = NULL;
  unsigned int logmode;

  CCSENTER("set_ccs_logging");

  ctx = xmlXPathNewContext(ldoc);
  if(!ctx){
    log_printf(LOG_ERR, "Error: unable to create new XPath context.\n");
    return -1;
  }

  logmode = logsys_config_mode_get();

  if(!debug) {
    res = do_simple_xml_query(ctx, "/cluster/logging/@debug");
    if(res) {
      if(!strcmp(res, "on")) {
	global_debug = 1;
      } else
      if(!strcmp(res, "off")) {
	global_debug = 0;
      } else
	log_printf(LOG_ERR, "global debug: unknown value\n");
      free(res);
      res=NULL;
    }

    res = do_simple_xml_query(ctx, "/cluster/logging/logger_subsys[@subsys=\"CCS\"]/@debug");
    if(res) {
      if(!strcmp(res, "on")) {
	debug = 1;
      } else
      if(!strcmp(res, "off")) { /* debug from cmdline/envvars override config */
	debug = 0;
      } else
	log_printf(LOG_ERR, "subsys debug: unknown value\n");
      free(res);
      res=NULL;
    } else
      debug = global_debug; /* global debug overrides subsystem only if latter is not specified */

    res = do_simple_xml_query(ctx, "/cluster/logging/logger_subsys[@subsys=\"CCS\"]/@syslog_level");
    if(res) {
      loglevel = logsys_priority_id_get (res);
      if (loglevel < 0)
	loglevel = LOG_LEVEL_INFO;

      if(!debug) {
	if(loglevel == LOG_LEVEL_DEBUG)
		debug = 1;

	logsys_config_priority_set (loglevel);
      }

      free(res);
      res=NULL;
    }
  } else
    logsys_config_priority_set (LOG_LEVEL_DEBUG);

  res = do_simple_xml_query(ctx, "/cluster/logging/@to_stderr");
  if(res) {
    if(!strcmp(res, "yes")) {
      logmode |= LOG_MODE_OUTPUT_STDERR;
    } else
    if(!strcmp(res, "no")) {
      logmode &= ~LOG_MODE_OUTPUT_STDERR;
    } else
      log_printf(LOG_ERR, "to_stderr: unknown value\n");
    free(res);
    res=NULL;
  }

  res = do_simple_xml_query(ctx, "/cluster/logging/@to_syslog");
  if(res) {
    if(!strcmp(res, "yes")) {
      logmode |= LOG_MODE_OUTPUT_SYSLOG_THREADED;
    } else
    if(!strcmp(res, "no")) {
      logmode &= ~LOG_MODE_OUTPUT_SYSLOG_THREADED;
    } else
      log_printf(LOG_ERR, "to_syslog: unknown value\n");
    free(res);
    res=NULL;
  }

  res = do_simple_xml_query(ctx, "/cluster/logging/@to_file");
  if(res) {
    if(!strcmp(res, "yes")) {
      logmode |= LOG_MODE_OUTPUT_FILE;
    } else
    if(!strcmp(res, "no")) {
      logmode &= ~LOG_MODE_OUTPUT_FILE;
    } else
      log_printf(LOG_ERR, "to_file: unknown value\n");
    free(res);
    res=NULL;
  }

  res = do_simple_xml_query(ctx, "/cluster/logging/@filename");
  if(res) {
    if(logsys_config_file_set(&error, res))
      log_printf(LOG_ERR, "filename: unable to open %s for logging\n", res);
    free(res);
    res=NULL;
  } else
      log_printf(LOG_DEBUG, "filename: use default built-in log file: %s\n", LOGDIR "/ccs.log");

  res = do_simple_xml_query(ctx, "/cluster/logging/@syslog_facility");
  if(res) {
    facility = logsys_facility_id_get (res);
    if (facility < 0) {
      log_printf(LOG_ERR, "syslog_facility: unknown value\n");
      facility = SYSLOGFACILITY;
    }

    logsys_config_facility_set ("CCS", facility);
    log_printf(LOG_DEBUG, "log_facility: %s (%d).\n", res, facility);
    free(res);
    res=NULL;
  }

  if(ctx){
    xmlXPathFreeContext(ctx);
  }

  if(logmode & LOG_MODE_BUFFER_BEFORE_CONFIG) {
    log_printf(LOG_DEBUG, "logsys config enabled from set_ccs_logging\n");
    logmode &= ~LOG_MODE_BUFFER_BEFORE_CONFIG;
    logmode |= LOG_MODE_FLUSH_AFTER_CONFIG;
    logsys_config_mode_set (logmode);
  }

  CCSEXIT("set_ccs_logging");
  return 0;
}
