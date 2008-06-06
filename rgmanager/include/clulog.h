/** @file
 * Header for clulog.c
 */

#ifndef __CLUSTER_LOG_H
#define __CLUSTER_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <syslog.h>
#include <sys/types.h>

#define LOGLEVEL_DFLT         LOG_NOTICE
#define MAX_LOGMSG_LEN        512

/*
 * int clu_set_loglevel(int severity)
 *
 * DESCRIPTION
 *   Set the logging level for this daemon.  This is not a 
 *   system-wide setting.
 *
 * ARGUMENTS
 *   severity  Severity as documented in sys/syslog.h (i.e. LOG_ERR)
 *
 * RETURN VALUES
 *   On success, the previous loglevel is returned.  On error -1 is returned.
 *
 * NOTES
 *   The only way of generating errors for this call is to give a negative
 *   value for severity.  Currently, syslog lists severities up to 8, but
 *   I see no reason for this restriction if, in the future, we decided to
 *   add more levels.  Thus, any number up to MAXINT will be supported.
 */
int clu_set_loglevel(int severity);
int clu_set_facility(char *facility);
int clu_log_console(int onoff);

/*
 * int clu_get_loglevel(void)
 *
 * DESCRIPTION
 *   Get the current logging level.
 *
 * ARGUMENTS
 *   none
 *
 * RETURN VALUES
 *   The current logging level is returned.
 */
int clu_get_loglevel(void);

/*
 * DESCRIPTION
 *   Cluster logging facility.  This is the actual function that does the
 *   logging.  No one should call this, you should call the wrappers provided.
 *   i.e. clulog and clulog_and_print.
 */
int do_clulog(int severity, int write_to_cons, pid_t pid,
	      char *prog, const char *fmt, ...);
/*
 * int clulog(int severity, const char *fmt, ...)
 *
 * DESCRIPTION
 *   Cluster logging facility.  This is a library routine which sends the 
 *   supplied parameters to the syslog daemon.  If the supplied severity is 
 *   numerically larger than the current loglevel, the message is never sent 
 *   to the log.
 *
 * ARGUMENTS
 *   severity  Severity as documented in sys/syslog.h (i.e. LOG_ERR)
 *   fmt       Format string as used with printf.
 *
 * RETURN VALUES
 *   On success, 0 is returned.  On error, -1 is returned.
 *
 * NOTES
 *   Inability to contact the logging daemon is the only source of error
 *   for this function.  Thus, it would behoove you to try a clulog before
 *   daemonizing your process.  If it fails, print a message to stderr
 *   explaining that the cluster logging daemon should probably be started.
 *   If you really want your message to be heard by someone, use
 *   clulog_and_print().
 */
#define clulog(x,fmt,args...)              do_clulog(x,0,0,NULL,fmt,##args)
#define clulog_pid(x,pid,prog,fmt,args...) do_clulog(x,0,pid,prog,fmt,##args)

/*
 * int clulog_and_print(int severity, int write_to_cons, const char *fmt, ...)
 *
 * DESCRIPTION
 *   Cluster logging facility.  This is a library routine which sends the 
 *   supplied parameters to the syslog daemon.  If the supplied severity is 
 *   numerically larger than the current loglevel, the message is never sent 
 *   to the log.  This version also prints the given message to the terminal.
 *
 * ARGUMENTS
 *   severity       Severity as documented in sys/syslog.h (i.e. LOG_ERR)
 *   fmt            Format string as used with printf.
 *
 * RETURN VALUES
 *   On success, 0 is returned.  On error, -1 is returned.
 */
#define clulog_and_print(x,fmt,args...)   do_clulog(x,1,0,NULL,fmt,##args)


/*
 * void clulog_close(void)
 *
 * DESCRIPTION
 *   This is an optional call to close the logfile.  This translates into a
 *   closelog() call.
 *
 * ARGUMENTS
 *   none
 *
 * RETURN VALUES
 *   This function does not return anything.
 */
void clulog_close(void);


#ifdef __cplusplus
}
#endif
#endif				/* __CLUSTER_LOG_H */
/*
 * Local variables:
 *  c-basic-offset: 8
 *  c-indent-level: 8
 *  tab-width: 8
 * End:
 */
