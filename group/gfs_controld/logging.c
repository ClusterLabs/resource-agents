#include "gfs_daemon.h"
#include "config.h"
#include "ccs.h"

extern int ccs_handle;

#define DAEMON_NAME "gfs_controld"
#define DEFAULT_LOG_MODE LOG_MODE_OUTPUT_FILE|LOG_MODE_OUTPUT_SYSLOG
#define DEFAULT_SYSLOG_FACILITY		SYSLOGFACILITY
#define DEFAULT_SYSLOG_PRIORITY		SYSLOGLEVEL
#define DEFAULT_LOGFILE_PRIORITY	LOG_INFO /* ? */
#define DEFAULT_LOGFILE			LOGDIR "/" DAEMON_NAME ".log"

static int log_mode;
static int syslog_facility;
static int syslog_priority;
static int logfile_priority;
static char logfile[PATH_MAX];

void init_logging(void)
{
	log_mode = DEFAULT_LOG_MODE;
	syslog_facility = DEFAULT_SYSLOG_FACILITY;
	syslog_priority = DEFAULT_SYSLOG_PRIORITY;
	logfile_priority = DEFAULT_LOGFILE_PRIORITY;
	strcpy(logfile, DEFAULT_LOGFILE);

	/* logfile_priority is the only one of these options that
	   can be controlled from command line or environment variable */

	if (cfgd_debug_logfile)
		logfile_priority = LOG_DEBUG;

	log_debug("logging mode %d syslog f %d p %d logfile p %d %s",
		  log_mode, syslog_facility, syslog_priority,
		  logfile_priority, logfile);

	logt_init(DAEMON_NAME, log_mode, syslog_facility, syslog_priority,
		  logfile_priority, logfile);
}

void setup_logging(void)
{
	ccs_read_logging(ccs_handle, DAEMON_NAME,
			 &cfgd_debug_logfile, &log_mode,
			 &syslog_facility, &syslog_priority,
			 &logfile_priority, logfile);

	log_debug("logging mode %d syslog f %d p %d logfile p %d %s",
		  log_mode, syslog_facility, syslog_priority,
		  logfile_priority, logfile);

	logt_conf(DAEMON_NAME, log_mode, syslog_facility, syslog_priority,
		  logfile_priority, logfile);
}

void close_logging(void)
{
	logt_exit();
}

