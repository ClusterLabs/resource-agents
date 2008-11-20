#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <libcman.h>
#include <ccs.h>
#include <liblogthread.h>

#include "copyright.cf"

int debug = 0;
int daemonize = 1;
int daemon_quit = 0;
cman_handle_t cman_handle;
int rr = 0;

#define LOCKFILE_NAME	"/var/run/cmannotifyd.pid"

#define OPTION_STRING "hdfVr"

#ifndef MAX_ARGS
#define MAX_ARGS	128
#endif

static void print_usage()
{
	printf("Usage:\n\n");
	printf("cmannotifyd [options]\n\n");
	printf("Options:\n\n");
	printf("  -f        Do not fork in background\n");
	printf("  -d        Enable debugging output\n");
	printf("  -r        Run Real Time priority\n");
	printf("  -h        This help\n");
	printf("  -V        Print program version information\n");
	return;
}

static void read_arguments(int argc, char **argv)
{
	int cont = 1;
	int optchar;

	while (cont) {
		optchar = getopt(argc, argv, OPTION_STRING);

		switch (optchar) {

		case 'd':
			debug = 1;
			break;

		case 'f':
			daemonize = 0;
			break;

		case 'r':
			rr = 1;
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case 'V':
			printf("cmannotifyd %s (built %s %s)\n%s\n",
			       RELEASE_VERSION, __DATE__, __TIME__,
			       REDHAT_COPYRIGHT);
			exit(EXIT_SUCCESS);
			break;

		case EOF:
			cont = 0;
			break;

		default:
			fprintf(stderr, "unknown option: %c\n", optchar);
			print_usage();
			exit(EXIT_FAILURE);
			break;

		}

	}
}

static void lockfile(void)
{
	int fd, error;
	struct flock lock;
	char buf[128];

	memset(buf, 0, 128);

	fd = open(LOCKFILE_NAME, O_CREAT | O_WRONLY,
		  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (fd < 0) {
		fprintf(stderr, "cannot open/create lock file %s\n",
			LOCKFILE_NAME);
		exit(EXIT_FAILURE);
	}

	lock.l_type = F_WRLCK;
	lock.l_start = 0;
	lock.l_whence = SEEK_SET;
	lock.l_len = 0;

	error = fcntl(fd, F_SETLK, &lock);
	if (error) {
		fprintf(stderr, "cmannotifyd is already running\n");
		exit(EXIT_FAILURE);
	}

	error = ftruncate(fd, 0);
	if (error) {
		fprintf(stderr, "cannot clear lock file %s\n", LOCKFILE_NAME);
		exit(EXIT_FAILURE);
	}

	sprintf(buf, "%d\n", getpid());

	error = write(fd, buf, strlen(buf));
	if (error <= 0) {
		fprintf(stderr, "cannot write lock file %s\n", LOCKFILE_NAME);
		exit(EXIT_FAILURE);
	}
}

static void sigterm_handler(int sig)
{
	daemon_quit = 1;
}

static void set_oom_adj(int val)
{
	FILE *fp;

	fp = fopen("/proc/self/oom_adj", "w");
	if (!fp)
		return;

	fprintf(fp, "%i", val);
	fclose(fp);
}

static void set_scheduler(void)
{
	struct sched_param sched_param;
	int rv;

	rv = sched_get_priority_max(SCHED_RR);
	if (rv != -1) {
		sched_param.sched_priority = rv;
		rv = sched_setscheduler(0, SCHED_RR, &sched_param);
		if (rv == -1)
			logt_print(LOG_WARNING,
				   "could not set SCHED_RR priority %d err %d",
				   sched_param.sched_priority, errno);
	} else {
		logt_print(LOG_WARNING,
			   "could not get maximum scheduler priority err %d",
			   errno);
	}
}

static void init_logging(int reconf)
{
	int ccs_handle;
	int mode = LOG_MODE_OUTPUT_FILE | LOG_MODE_OUTPUT_SYSLOG;
	int syslog_facility = SYSLOGFACILITY;
	int syslog_priority = SYSLOGLEVEL;
	char logfile[PATH_MAX];
	int logfile_priority = SYSLOGLEVEL;

	memset(logfile, 0, PATH_MAX);
	sprintf(logfile, LOGDIR "/cmannotifyd.log");

	ccs_handle = ccs_connect();
	if (ccs_handle > 0) {
		ccs_read_logging(ccs_handle, "cmannotifyd", &debug, &mode,
				 &syslog_facility, &syslog_priority, &logfile_priority, logfile);
		ccs_disconnect(ccs_handle);
	}

#if 0
	if (!daemonize)
		mode |= LOG_MODE_OUTPUT_STDERR;
#endif

	if (!reconf)
		logt_init("cmannotifyd", mode, syslog_facility, syslog_priority, logfile_priority, logfile);
	else
		logt_conf("cmannotifyd", mode, syslog_facility, syslog_priority, logfile_priority, logfile);
}

static void dispatch_notification(char *str, int *quorum)
{
	char *envp[MAX_ARGS];
	char *argv[MAX_ARGS];
	int envptr = 0;
	int argvptr = 0;
	char scratch[PATH_MAX];
	pid_t notify_pid;
	int pidstatus;

	if (!str)
		return;

	/* pass notification type */
	snprintf(scratch, sizeof(scratch), "CMAN_NOTIFICATION=%s", str);
	envp[envptr++] = strdup(scratch);

	if (quorum) {
		snprintf(scratch, sizeof(scratch), "CMAN_NOTIFICATION_QUORUM=%d", *quorum);
		envp[envptr++] = strdup(scratch);
	}

	if (debug)
		envp[envptr++] = strdup("CMAN_NOTIFICATION_DEBUG=1");

	envp[envptr++] = NULL;

	argv[argvptr++] = "cman_notify";

	argv[argvptr++] = NULL;

	switch ( (notify_pid = fork()) )
	{
		case -1:
			/* unable to fork */
			exit(EXIT_FAILURE);
			break;

		case 0: /* child */
			execve(SBINDIR "/cman_notify", argv, envp);
			/* unable to execute cman_notify */
			exit(EXIT_FAILURE);
			break;

		default: /* parent */
			waitpid(notify_pid, &pidstatus, 0);
			break;
	}

}

static void cman_callback(cman_handle_t ch, void *private, int reason, int arg)
{
	char *str = NULL;

	switch (reason) {
	case CMAN_REASON_TRY_SHUTDOWN:
		logt_print(LOG_DEBUG, "Received a cman shutdown request\n");
		cman_replyto_shutdown(ch, 1);	/* allow cman to shutdown */
		str = "CMAN_REASON_TRY_SHUTDOWN";
		dispatch_notification(str, 0);
		break;
	case CMAN_REASON_STATECHANGE:
		logt_print(LOG_DEBUG,
			   "Received a cman statechange notification\n");
		str = "CMAN_REASON_STATECHANGE";
		dispatch_notification(str, &arg);
		break;
	case CMAN_REASON_CONFIG_UPDATE:
		logt_print(LOG_DEBUG,
			   "Received a cman config update notification\n");
		init_logging(1);
		str = "CMAN_REASON_CONFIG_UPDATE";
		dispatch_notification(str, 0);
		break;
	}
}

static void byebye_cman()
{
	if (!cman_handle)
		return;

	cman_finish(cman_handle);
	cman_handle = NULL;
}

static void setup_cman(int forever)
{
	int init = 0, active = 0;

retry_init:
	cman_handle = cman_init(NULL);
	if (!cman_handle) {
		if ((init++ < 5) || (forever)) {
			if (daemon_quit)
				goto out;

			sleep(1);
			goto retry_init;
		}
		logt_print(LOG_CRIT, "cman_init error %d\n", errno);
		exit(EXIT_FAILURE);
	}

retry_active:
	if (!cman_is_active(cman_handle)) {
		if ((active++ < 5) || (forever)) {
			if (daemon_quit)
				goto out;

			sleep(1);
			goto retry_active;
		}
		logt_print(LOG_CRIT, "cman_is_active error %d\n", errno);
		cman_finish(cman_handle);
		exit(EXIT_FAILURE);
	}

	if (cman_start_notification(cman_handle, cman_callback) < 0) {
		logt_print(LOG_CRIT, "cman_start_notification error %d\n", errno);
		cman_finish(cman_handle);
		exit(EXIT_FAILURE);
	}

	return;

out:
	byebye_cman();
	exit(EXIT_SUCCESS);
}

static void loop()
{
	int rv;

	for (;;) {
		rv = cman_dispatch(cman_handle, CMAN_DISPATCH_ONE);
		if (rv == -1 && errno == EHOSTDOWN) {
			byebye_cman();
			logt_print(LOG_DEBUG, "waiting for cman to reappear..\n");
			setup_cman(1);
			logt_print(LOG_DEBUG, "cman is back..\n");
		}

		if (daemon_quit) {
			logt_print(LOG_DEBUG, "shutting down...\n");
			byebye_cman();
			exit(EXIT_SUCCESS);
		}

		sleep(1);
	}
}

int main(int argc, char **argv)
{

	read_arguments(argc, argv);
	lockfile();

	if (daemonize) {
		if (daemon(0, 0) < 0) {
			perror("Unable to daemonize");
			exit(EXIT_FAILURE);
		}
	}

	init_logging(0);
	signal(SIGTERM, sigterm_handler);
	set_oom_adj(-16);
	if (rr)
		set_scheduler();

	setup_cman(0);
	loop();

	return 0;
}
