/** @file
 * Library routines for communicating with the logging daemon.
 *
 *  $Id$
 *
 *  Author: Jeff Moyer <moyer@missioncriticallinux.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <malloc.h>
#include <dirent.h>
#include <signal.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/socket.h>
#define SYSLOG_NAMES
#include <sys/syslog.h>
#undef SYSLOG_NAMES

#include <sys/wait.h>
#include <sys/types.h>
#include <linux/unistd.h>
#include <pthread.h>
#include <gettid.h>
#include <clulog.h>
#include <string.h>


static const char *version __attribute__ ((unused)) = "$Revision$";

#ifdef DEBUG
#include <assert.h>
#define Dprintf(fmt,args...) printf(fmt,##args)
#define DBG_ASSERT(x)  assert(x)
#else
#define Dprintf(fmt,args...)
#define DBG_ASSERT(x)
#endif

/*
 * Globals
 */
static int   log_is_open = 0;
static int   useconsole = 0;
static int   loglevel = LOGLEVEL_DFLT;
static int   syslog_facility = LOG_DAEMON;
static char  *daemon_name = NULL;
static pid_t daemon_pid = -1;

#ifdef WRAP_LOCKS
static pthread_mutex_t log_mutex = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;
#else
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

CODE logger_prioritynames[] = 
{ {"emerg", LOG_EMERG},
  {"alert", LOG_ALERT},
  {"crit", LOG_CRIT},
  {"err", LOG_ERR},
  {"warning", LOG_WARNING},
  {"notice", LOG_NOTICE},
  {"info", LOG_INFO},
  {"debug", LOG_DEBUG}
};

/*
 *  Exported Functions.
 */

/**
 * @return The current cluster log level.
 */
int
clu_get_loglevel(void)
{
	return loglevel;
}


/**
 * Set the cluster log level.
 *
 * @param severity	New log level.
 * @return 		Old log level, or -1 if 'severity' is an invalid log
 *			level.
 */
int
clu_set_loglevel(int severity)
{
	int ret = loglevel;

	if (severity > 0) {
		loglevel = severity;
		return ret;
	}

	return -1;
}


/**
 * @return The current cluster log facility.
 */
char *
clu_get_facility(void)
{
	int x = 0;

	pthread_mutex_lock(&log_mutex);
	for (; facilitynames[x].c_name; x++) {
		if (syslog_facility == facilitynames[x].c_val) {
			pthread_mutex_unlock(&log_mutex);
			return facilitynames[x].c_name;
		}
	}
	
	pthread_mutex_unlock(&log_mutex);
	return "local4";
}


/**
 * Set the cluster log facility.
 *
 * @param facilityname  New log facility (see /usr/include/sys/syslog.h).
 * @return 		0
 */
int
clu_set_facility(char *facilityname)
{
	int x = 0, old;

	pthread_mutex_lock(&log_mutex);
	old = syslog_facility;

	for (; facilitynames[x].c_name; x++) {
		if (strcmp(facilityname, facilitynames[x].c_name))
			continue;

		syslog_facility = facilitynames[x].c_val;
		break;
	}

	if (syslog_facility == old) {
		pthread_mutex_unlock(&log_mutex);
		return 0;
	}

	closelog();
	log_is_open = 0;
	pthread_mutex_unlock(&log_mutex);
	return 0;
}


/**
 * Set the console logging mode.  Does not work for daemons.
 *
 * @param onoff		0 = off, otherwise on.
 * @return		Old log-to-console state.
 */
int
clu_log_console(int onoff)
{
	int ret = useconsole;

	useconsole = !!onoff;
	return ret;
}


/**
 * Cluster logging function.  Talks to syslog and writes to the
 * console, if necessary.
 */
int
do_clulog(int        severity,
	  int        write_to_cons,
	  pid_t      pid,
	  char       *prog,
	  const char *fmt, ...)
{
	va_list      args;
	char         logmsg[MAX_LOGMSG_LEN];	/* message to go to the log */
	char         printmsg[MAX_LOGMSG_LEN];	/* message to go to stdout */
	int          syslog_flags = LOG_NDELAY;

	pthread_mutex_lock(&log_mutex);
	if (severity > loglevel) {
		pthread_mutex_unlock(&log_mutex);
		return 0;
	}

	memset(logmsg, 0, MAX_LOGMSG_LEN);
	memset(printmsg, 0, MAX_LOGMSG_LEN);

	/*
	 * Check to see if the caller has forked.
	 */
	if (!pid) {

		/* Use thread IDs */
		if (daemon_pid != gettid()) {

			daemon_pid = gettid();
			log_is_open = 0;
		}

		syslog_flags |= LOG_PID;

	} else {

		daemon_pid = pid;
		closelog();
		log_is_open = 0;
		snprintf(logmsg, MAX_LOGMSG_LEN, "[%d]: ", pid);
	}

	if (prog) {

		if (daemon_name) {

			free(daemon_name);
			daemon_name = NULL;
		}

		daemon_name = strdup(prog);
	}

	if (!log_is_open) {

		openlog(daemon_name, syslog_flags, syslog_facility);
		log_is_open = 1;
	}
	/*
	 * Note: This can be called in the context of a CGI program, in which
	 * case anything printed to stdout goes to the web page.  This can
	 * cause problems if we have our standard <warning> strings b/c
	 * the web client will try to interpret this as an html tag.
	 */
	snprintf(logmsg + strlen(logmsg), MAX_LOGMSG_LEN - strlen(logmsg), 
		 "<%s> ", logger_prioritynames[severity].c_name);

	va_start(args, fmt);
	vsnprintf(logmsg + strlen(logmsg), MAX_LOGMSG_LEN - strlen(logmsg), 
		  fmt, args);
	va_end(args);

	if (write_to_cons || useconsole) {
		snprintf(printmsg, MAX_LOGMSG_LEN, "[%d] %s: ", daemon_pid,
			 logger_prioritynames[severity].c_name);

		va_start(args, fmt);
		vsnprintf(printmsg + strlen(printmsg),
			  MAX_LOGMSG_LEN - strlen(printmsg), fmt, args);
		va_end(args);

		fprintf(stdout, "%s", printmsg);
	}

	syslog(severity, "%s", logmsg);

	pthread_mutex_unlock(&log_mutex);

	return 0;
}


/**
 * Stop the cluster logging facility.
 */
void
clulog_close(void)
{
	closelog();
}
