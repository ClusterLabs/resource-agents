#include <stdlib.h>
#include <logging.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <malloc.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ccs.h>

#define DAEMON_NAME "rgmanager"

/* default: errors go to syslog (/var/log/messages) and <daemon>.log
   logging/debug=on: errors continue going to syslog (/var/log/messages)
   and <daemon>.log, debug messages are added to <daemon>.log. */

#define DEFAULT_MODE		LOG_MODE_OUTPUT_SYSLOG_THREADED | \
				LOG_MODE_OUTPUT_FILE | \
				LOG_MODE_NOSUBSYS | \
				LOG_MODE_FILTER_DEBUG_FROM_SYSLOG
static int default_mode = DEFAULT_MODE;

#define DEFAULT_FACILITY	SYSLOGFACILITY /* cluster config setting */
#define DEFAULT_PRIORITY	SYSLOGLEVEL /* cluster config setting */
static int default_priority = DEFAULT_PRIORITY;

#define DEFAULT_FILE		LOGDIR "/" DAEMON_NAME ".log"

#define DAEMON_LEVEL_PATH "/cluster/logging/logger_subsys[@subsys=\"rgmanager\"]/@syslog_level"
#define DAEMON_DEBUG_PATH "/cluster/logging/logger_subsys[@subsys=\"rgmanager\"]/@debug"

/* Read cluster.conf settings and convert them into logsys values.
   If no cluster.conf setting exists, the default that was used in
   logsys_init() is used.

   mode from
   "/cluster/logging/@to_stderr"
   "/cluster/logging/@to_syslog"
   "/cluster/logging/@to_file"

   facility from
   "/cluster/logging/@syslog_facility"

   priority from
   "/cluster/logging/logger_subsys[@subsys=\"prog_name\"]/@syslog_level"

   file from
   "/cluster/logging/@logfile"

   debug from
   "/cluster/logging/@debug"
   "/cluster/logging/logger_subsys[@subsys=\"prog_name\"]/@debug"
*/
static void
read_ccs_name(int ccs_handle, char *path, char *name, size_t max)
{
	char *str;
	int error;

	name[max-1] = 0;

	error = ccs_get(ccs_handle, path, &str);
	if (error || !str)
		return;

	strncpy(name, str, max-1);

	free(str);
}


static int
read_ccs_yesno(int ccs_handle, char *path)
{
	char *str = NULL;
	int error;
	int ret = -1;

	error = ccs_get(ccs_handle, path, &str);
	if (error)
		goto out;

	if ((!strcasecmp(str, "yes")) ||
	    (!strcasecmp(str, "true")) ||
	    (atoi(str) > 0)) {
		ret = 1;
	}

	if ((!strcasecmp(str, "no")) ||
	    (!strcasecmp(str, "false")) ||
	    (atoi(str) == 0)) {
		ret = 0;
	}

out:
	if (str) 
		free(str);
	return ret;
}


static int
read_ccs_logging(int ccs_handle, int *mode, int *facility, int *priority,
		 char *file)
{
	int val;
	int m = 0, f = 0, p = 0;
	char name[PATH_MAX];

	/*
	 * mode
	 */

	m = default_mode;
	val = read_ccs_yesno(ccs_handle, "/cluster/logging/@to_stderr");
	if (val == 1)
		m |= LOG_MODE_OUTPUT_STDERR;
	else if (val == 0)
		m &= ~LOG_MODE_OUTPUT_STDERR;

	val = read_ccs_yesno(ccs_handle, "/cluster/logging/@to_stderr");
	if (val == 1)
		m |= LOG_MODE_OUTPUT_SYSLOG_THREADED;
	else if (val == 0)
		m &= ~LOG_MODE_OUTPUT_SYSLOG_THREADED;

	val = read_ccs_yesno(ccs_handle, "/cluster/logging/@to_file");
	if (val == 1)
		m |= LOG_MODE_OUTPUT_FILE;
	else if (val == 0)
		m &= ~LOG_MODE_OUTPUT_FILE;

	*mode = m;

	/*
	 * facility
	 */

	f = DEFAULT_FACILITY;

	memset(name, 0, sizeof(name));
	read_ccs_name(ccs_handle, "/cluster/logging/@syslog_facility", name, sizeof(name));

	if (name[0]) {
		val = logsys_facility_id_get(name);
		if (val >= 0)
			f = val;
	}

	*facility = f;

	/*
	 * priority
	 */

	p = DEFAULT_PRIORITY;

	memset(name, 0, sizeof(name));
	read_ccs_name(ccs_handle, DAEMON_LEVEL_PATH, name, sizeof(name));

	if (name[0]) {
		val = logsys_priority_id_get(name);
		if (val >= 0)
			p = val;
	}

	*priority = p;

	/*
	 * file
	 */

	strcpy(file, DEFAULT_FILE);

	memset(name, 0, sizeof(name));
	read_ccs_name(ccs_handle, "/cluster/logging/@logfile", name, sizeof(name));

	if (name[0])
		strcpy(file, name);

	/*
	 * debug
	 */
	memset(name, 0, sizeof(name));
	read_ccs_name(ccs_handle, "/cluster/logging/@debug", name, sizeof(name));

#if 0
	if (!strcmp(name, "on"))
		cfgd_debug_logsys = 1;

	memset(name, 0, sizeof(name));
	read_ccs_name(ccs_handle, DAEMON_DEBUG_PATH, name);

	if (!strcmp(name, "on"))
		cfgd_debug_logsys = 1;
	else if (!strcmp(name, "off"))
		cfgd_debug_logsys = 0;
#endif
	return 0;
}

/* initial settings until we can read cluster.conf logging settings from ccs */

void
init_logging(int foreground, int default_prio)
{
	if (foreground)
		default_mode |= LOG_MODE_OUTPUT_STDERR;
	if (default_prio >= 0)
		default_priority = default_prio;
	logsys_init(DAEMON_NAME, default_mode, DEFAULT_FACILITY,
		    DEFAULT_PRIORITY, DEFAULT_FILE);
}

/* this function is also called when we get a cman config-update event */
void
setup_logging(int ccs_handle)
{
	int mode, facility, priority;
	char file[PATH_MAX];

	memset(file, 0, PATH_MAX);

	read_ccs_logging(ccs_handle, &mode, &facility, &priority, file);
	logsys_conf(DAEMON_NAME, mode, facility, priority, file);
}

void
close_logging(void)
{
	logsys_exit();
}

