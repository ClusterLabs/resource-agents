#include "fd.h"
#include "ccs.h"

LOGSYS_DECLARE_SUBSYS ("FENCED", LOG_LEVEL_INFO);

static int open_ccs(void)
{
	int i = 0, cd;

	while ((cd = ccs_connect()) < 0) {
		sleep(1);
		if (++i > 9 && !(i % 10))
			log_printf(LOG_ERR, "connect to ccs error %d, "
				  "check ccsd or cluster status", cd);
	}
	return cd;
}

static void read_ccs_int(int cd, char *path, int *config_val)
{
	char *str;
	int val;
	int error;

	error = ccs_get(cd, path, &str);
	if (error || !str)
		return;

	val = atoi(str);

	if (val < 0) {
		log_printf(LOG_ERR, "ignore invalid value %d for %s", val, path);
		return;
	}

	*config_val = val;
	log_printf_debug("%s is %u", path, val);
	free(str);
}

int get_logsys_config_data(void)
{
	int ccsfd = -1, loglevel = LOG_LEVEL_INFO, facility = SYSLOGFACILITY;
	char *val = NULL, *error = NULL;
	unsigned int logmode;
	int global_debug = 0;

	log_printf(LOG_DEBUG, "Loading logsys configuration information\n");

	ccsfd = ccs_connect();
	if (ccsfd < 0) {
		log_printf(LOG_CRIT, "Connection to CCSD failed; cannot start\n");
		return -1;
	}

	logmode = logsys_config_mode_get();

	if (!daemon_debug_opt) {
		if (ccs_get(ccsfd, "/cluster/logging/@debug", &val) == 0) {
			if(!strcmp(val, "on")) {
				global_debug = 1;
			} else
			if(!strcmp(val, "off")) {
				global_debug = 0;
			} else
				log_printf(LOG_ERR, "global debug: unknown value\n");
			free(val);
			val = NULL;
		}

		if (ccs_get(ccsfd, "/cluster/logging/logger_subsys[@subsys=\"FENCED\"]/@debug", &val) == 0) {
			if(!strcmp(val, "on")) {
				daemon_debug_opt = 1;
			} else
			if(!strcmp(val, "off")) { /* debug from cmdline/envvars override config */
				daemon_debug_opt = 0;
			} else
				log_printf(LOG_ERR, "subsys debug: unknown value: %s\n", val);
			free(val);
			val = NULL;
		} else
			daemon_debug_opt = global_debug; /* global debug overrides subsystem only if latter is not specified */

		if (ccs_get(ccsfd, "/cluster/logging/logger_subsys[@subsys=\"FENCED\"]/@syslog_level", &val) == 0) {
			loglevel = logsys_priority_id_get (val);
			if (loglevel < 0)
				loglevel = LOG_LEVEL_INFO;

			if (!daemon_debug_opt) {
				if (loglevel == LOG_LEVEL_DEBUG)
					daemon_debug_opt = 1;

				logsys_config_priority_set (loglevel);
			}

			free(val);
			val = NULL;
		}
	} else
		logsys_config_priority_set (LOG_LEVEL_DEBUG);

	if (ccs_get(ccsfd, "/cluster/logging/@to_stderr", &val) == 0) {
		if(!strcmp(val, "yes")) {
			logmode |= LOG_MODE_OUTPUT_STDERR;
		} else
		if(!strcmp(val, "no")) {
			logmode &= ~LOG_MODE_OUTPUT_STDERR;
		} else
			log_printf(LOG_ERR, "to_stderr: unknown value\n");
		free(val);
		val = NULL;
	}

	if (ccs_get(ccsfd, "/cluster/logging/@to_syslog", &val) == 0) {
		if(!strcmp(val, "yes")) {
			logmode |= LOG_MODE_OUTPUT_SYSLOG_THREADED;
		} else
		if(!strcmp(val, "no")) {
			logmode &= ~LOG_MODE_OUTPUT_SYSLOG_THREADED;
		} else
			log_printf(LOG_ERR, "to_syslog: unknown value\n");
		free(val);
		val = NULL;
	}

	if (ccs_get(ccsfd, "/cluster/logging/@to_file", &val) == 0) {
		if(!strcmp(val, "yes")) {
			logmode |= LOG_MODE_OUTPUT_FILE;
		} else
		if(!strcmp(val, "no")) {
			logmode &= ~LOG_MODE_OUTPUT_FILE;
		} else
			log_printf(LOG_ERR, "to_file: unknown value\n");
		free(val);
		val = NULL;
	}

	if (ccs_get(ccsfd, "/cluster/logging/@filename", &val) == 0) {
		if(logsys_config_file_set(&error, val))
			log_printf(LOG_ERR, "filename: unable to open %s for logging\n", val);
		free(val);
		val = NULL;
	} else
		log_printf(LOG_DEBUG, "filename: use default built-in log file: %s\n", LOGDIR "/fenced.log");

	if (ccs_get(ccsfd, "/cluster/logging/@syslog_facility", &val) == 0) {
		facility = logsys_facility_id_get (val);
		if (facility < 0) {
			log_printf(LOG_ERR, "syslog_facility: unknown value\n");
			facility = SYSLOGFACILITY;
		}

		logsys_config_facility_set ("FENCED", facility);
		free(val);
		val = NULL;
	}

	if(logmode & LOG_MODE_BUFFER_BEFORE_CONFIG) {
		log_printf(LOG_DEBUG, "logsys config enabled from get_logsys_config_data\n");
		logmode &= ~LOG_MODE_BUFFER_BEFORE_CONFIG;
		logmode |= LOG_MODE_FLUSH_AFTER_CONFIG;
		logsys_config_mode_set (logmode);
	}

	ccs_disconnect(ccsfd);

	return 0;
}

#define OUR_NAME_PATH "/cluster/clusternodes/clusternode[@name=\"%s\"]/@name"
#define GROUPD_COMPAT_PATH "/cluster/group/@groupd_compat"
#define CLEAN_START_PATH "/cluster/fence_daemon/@clean_start"
#define POST_JOIN_DELAY_PATH "/cluster/fence_daemon/@post_join_delay"
#define POST_FAIL_DELAY_PATH "/cluster/fence_daemon/@post_fail_delay"
#define OVERRIDE_PATH_PATH "/cluster/fence_daemon/@override_path"
#define OVERRIDE_TIME_PATH "/cluster/fence_daemon/@override_time"

int read_ccs(struct fd *fd)
{
	char path[256];
	char *str;
	int error, cd, i = 0, count = 0;

	if(trylater)
		if(get_logsys_config_data())
			log_printf(LOG_ERR, "Unable to configure logging system\n");

	cd = open_ccs();
	if (cd < 0)
		return cd;

	/* Our own nodename must be in cluster.conf before we're allowed to
	   join the fence domain and then mount gfs; other nodes need this to
	   fence us. */

	str = NULL;
	memset(path, 0, 256);
	snprintf(path, 256, OUR_NAME_PATH, our_name);

	error = ccs_get(cd, path, &str);
	if (error || !str) {
		log_printf(LOG_ERR, "local cman node name \"%s\" not found in the "
			  "configuration", our_name);
		return error;
	}
	if (str)
		free(str);

	/* The comline config options are initially set to the defaults,
	   then options are read from the command line to override the
	   defaults, for options not set on command line, we look for
	   values set in cluster.conf. */

	if (!comline.groupd_compat_opt)
		read_ccs_int(cd, GROUPD_COMPAT_PATH, &comline.groupd_compat);
	if (!comline.clean_start_opt)
		read_ccs_int(cd, CLEAN_START_PATH, &comline.clean_start);
	if (!comline.post_join_delay_opt)
		read_ccs_int(cd, POST_JOIN_DELAY_PATH, &comline.post_join_delay);
	if (!comline.post_fail_delay_opt)
		read_ccs_int(cd, POST_FAIL_DELAY_PATH, &comline.post_fail_delay);
	if (!comline.override_time_opt)
		read_ccs_int(cd, OVERRIDE_TIME_PATH, &comline.override_time);

	if (!comline.override_path_opt) {
		str = NULL;
		memset(path, 0, 256);
		sprintf(path, OVERRIDE_PATH_PATH);

		error = ccs_get(cd, path, &str);
		if (!error && str) {
			free(comline.override_path);
			comline.override_path = strdup(str);
		}
		if (str)
			free(str);
	}

	if (comline.clean_start) {
		log_printf_debug("clean start, skipping initial nodes");
		goto out;
	}

	for (i = 1; ; i++) {
		str = NULL;
		memset(path, 0, 256);
		sprintf(path, "/cluster/clusternodes/clusternode[%d]/@nodeid", i);

		error = ccs_get(cd, path, &str);
		if (error || !str)
			break;

		add_complete_node(fd, atoi(str));
		free(str);
		count++;
	}

	log_printf_debug("added %d nodes from ccs", count);
 out:
	ccs_disconnect(cd);
	return 0;
}

