#include <stdio.h>
#include <stdlib.h>
#define SYSLOG_NAMES
#include <syslog.h>
#include <logging.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <malloc.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ccs.h>
#include <limits.h>

#define DAEMON_NAME "rgmanager"
static char daemon_name[PATH_MAX];

/* default: errors go to syslog (/var/log/messages) and <daemon>.log
   logging/debug=on: errors continue going to syslog (/var/log/messages)
   and <daemon>.log, debug messages are added to <daemon>.log. */

#define DEFAULT_MODE		LOG_MODE_OUTPUT_SYSLOG| \
				LOG_MODE_OUTPUT_FILE
static int default_mode = DEFAULT_MODE;

#define DEFAULT_FACILITY	SYSLOGFACILITY /* cluster config setting */
#define DEFAULT_PRIORITY	SYSLOGLEVEL /* cluster config setting */
static int default_priority = DEFAULT_PRIORITY;

#define DEFAULT_FILE		LOGDIR "/" DAEMON_NAME ".log"


void
init_logging(char *name, int foreground, int default_prio)
{
	if (!name)
		name = DAEMON_NAME;

	strncpy(daemon_name, name, PATH_MAX);

	if (foreground)
		default_mode |= LOG_MODE_OUTPUT_STDERR;
	if (default_prio >= 0)
		default_priority = default_prio;
	logt_init(name, default_mode, DEFAULT_FACILITY,
		  DEFAULT_PRIORITY, DEFAULT_PRIORITY, DEFAULT_FILE);
}


int
ccs_read_old_logging(int ccsfd, int *facility, int *priority)
{
	char query[256];
	char *val;
	int x, ret = 0;

	/* Get log log_facility */
	snprintf(query, sizeof(query), "/cluster/rm/@log_facility");
	if (ccs_get(ccsfd, query, &val) == 0) {
		for (x = 0; facilitynames[x].c_name; x++) {
			if (strcasecmp(val, facilitynames[x].c_name))
				continue;
			*facility = facilitynames[x].c_val;
			ret = 1;
			break;
		}
		free(val);
	}

	/* Get log level */
	snprintf(query, sizeof(query), "/cluster/rm/@log_level");
	if (ccs_get(ccsfd, query, &val) == 0) {
		*priority = atoi(val);
		free(val);
		if (*priority < 0)
			*priority = DEFAULT_PRIORITY;
		else
			ret = 1;
	}

	return ret;
}


/* this function is also called when we get a cman config-update event */
void
setup_logging(int ccs_handle)
{
	int mode = DEFAULT_MODE;
	int facility = DEFAULT_FACILITY;
	int loglevel = DEFAULT_PRIORITY, filelevel = DEFAULT_PRIORITY;
	int debug = 0;
	char file[PATH_MAX];

	memset(file, 0, PATH_MAX);
	snprintf(file, sizeof(file)-1, DEFAULT_FILE);
	if (ccs_read_old_logging(ccs_handle, &facility, &loglevel))
		filelevel = loglevel;

	ccs_read_logging(ccs_handle, (char *)DAEMON_NAME, &debug, &mode,
        		 &facility, &loglevel, &filelevel, (char *)file);

	/* clulog uses rgmanager's config but getppid()'s name */
	logt_conf(daemon_name, mode, facility, loglevel,
	    	      filelevel, file);
}

void
close_logging(void)
{
	logt_exit();
}
