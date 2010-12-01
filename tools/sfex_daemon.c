#include <config.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <limits.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <syslog.h>
#include "sfex.h"
#include "sfex_lib.h"

#if HAVE_GLUE_CONFIG_H
#include <glue_config.h> /* for HA_LOG_FACILITY */
#endif

static int sysrq_fd;
static int lock_index = 1;        /* default 1st lock */
static time_t collision_timeout = 1; /* default 1 sec */
static time_t lock_timeout = 60; /* default 60 sec */
time_t unlock_timeout = 60;
static time_t monitor_interval = 10;

static sfex_controldata cdata;
static sfex_lockdata ldata;
static sfex_lockdata ldata_new;

static const char *device;
const char *progname;
char *nodename;
static const char *rsc_id = "sfex";

static void usage(FILE *dist) {
	  fprintf(dist, "usage: %s [-i <index>] [-c <collision_timeout>] [-t <lock_timeout>] <device>\n", progname);
}

static void acquire_lock(void)
{
	if (read_lockdata(&cdata, &ldata, lock_index) == -1) {
		cl_log(LOG_ERR, "read_lockdata failed in acquire_lock\n");
		exit(EXIT_FAILURE);
	}

	if ((ldata.status == SFEX_STATUS_LOCK) && (strncmp(nodename, (const char*)(ldata.nodename), sizeof(ldata.nodename)))) {
		unsigned int t = lock_timeout;
		while (t > 0)
			t = sleep(t);
		read_lockdata(&cdata, &ldata_new, lock_index);
		if (ldata.count != ldata_new.count) {
			cl_log(LOG_ERR, "can\'t acquire lock: the lock's already hold by some other node.\n");
			exit(2);
		}
	}

	/* The lock acquisition is possible because it was not updated. */
	ldata.status = SFEX_STATUS_LOCK;
	ldata.count = SFEX_NEXT_COUNT(ldata.count);
	strncpy((char*)(ldata.nodename), nodename, sizeof(ldata.nodename));
	if (write_lockdata(&cdata, &ldata, lock_index) == -1) {
		cl_log(LOG_ERR, "write_lockdata failed\n");
		exit(EXIT_FAILURE);
	}

	/* detect the collision of lock */
	/* The collision occurs when two or more nodes do the reservation 
	   processing of the lock at the same time. It waits for collision_timeout 
	   seconds to detect this,and whether the superscription of lock data by 
	   another node is done is checked. If the superscription was done by 
	   another node, the lock acquisition with the own node is given up.  
	 */
	{
		unsigned int t = collision_timeout;
		while (t > 0)
			t = sleep(t);
		if (read_lockdata(&cdata, &ldata_new, lock_index) == -1) {
			cl_log(LOG_ERR, "read_lockdata failed in collision detection\n");
		}
		if (strncmp((char*)(ldata.nodename), (const char*)(ldata_new.nodename), sizeof(ldata.nodename))) {
			cl_log(LOG_ERR, "can\'t acquire lock: collision detected in the air.\n");
			exit(2);
		}
	}

	/* extension of lock */
	/* Validly time of the lock is extended. It is because of spending at 
	   the collision_timeout seconds to detect the collision. */
	ldata.count = SFEX_NEXT_COUNT(ldata.count);
	if (write_lockdata(&cdata, &ldata, lock_index) == -1) {
		cl_log(LOG_ERR, "write_lockdata failed in extension of lock\n");
		exit(EXIT_FAILURE);
	}
	cl_log(LOG_INFO, "lock acquired\n");
}

static void error_todo (void)
{
	if (fork() == 0) {
		cl_log(LOG_INFO, "Execute \"crm_resource -F -r %s -H %s\" command\n", rsc_id, nodename);
		execl("/usr/sbin/crm_resource", "crm_resource", "-F", "-r", rsc_id, "-H", nodename, NULL);
	} else {
		exit(EXIT_FAILURE);
	}
}

static void failure_todo(void)
{
#ifdef SFEX_TESTING	
	exit(EXIT_FAILURE);
#else
	/*execl("/usr/sbin/crm_resource", "crm_resource", "-F", "-r", rsc_id, "-H", nodename, NULL); */
	int ret;

	cl_log(LOG_INFO, "Force reboot node %s\n", nodename);
	ret = write(sysrq_fd, "b\n", 2);
	if (ret == -1) {
		cl_log(LOG_ERR, "%s\n", strerror(errno));
	}
	close(sysrq_fd);
	exit(EXIT_FAILURE);
#endif
}

static void update_lock(void)
{
	/* read lock data */
	if (read_lockdata(&cdata, &ldata, lock_index) == -1) {
		cl_log(LOG_ERR, "read_lockdata failed in update_lock\n");
		error_todo();
		exit(EXIT_FAILURE);
	}

	/* check current lock status */
	/* if own node is not locking, lock update is failed */
	if (ldata.status != SFEX_STATUS_LOCK || strncmp((const char*)(ldata.nodename), nodename, sizeof(ldata.nodename))) {
		cl_log(LOG_ERR, "can't update lock.\n");
		failure_todo();
		exit(EXIT_FAILURE); 
	}

	/* lock update */
	ldata.count = SFEX_NEXT_COUNT(ldata.count);
	if (write_lockdata(&cdata, &ldata, lock_index) == -1) {
		cl_log(LOG_ERR, "write_lockdata failed in update_lock\n");
		error_todo();
		exit(EXIT_FAILURE);
	}
}

static void release_lock(void)
{
	/* The only thing I care about in release_lock(), is to terminate the process */
	   
	/* read lock data */
	if (read_lockdata(&cdata, &ldata, lock_index) == -1) {
		cl_log(LOG_ERR, "read_lockdata failed in release_lock\n");
		exit(EXIT_FAILURE);
	}

	/* check current lock status */
	/* if own node is not locking, we judge that lock has been released already */
	if (ldata.status != SFEX_STATUS_LOCK || strncmp((const char*)(ldata.nodename), nodename, sizeof(ldata.nodename))) {
		cl_log(LOG_ERR, "lock was already released.\n");
		exit(EXIT_FAILURE);
	}

	/* lock release */
	ldata.status = SFEX_STATUS_UNLOCK;
	if (write_lockdata(&cdata, &ldata, lock_index) == -1) {
	    /*FIXME: We are going to self-stop */
		cl_log(LOG_ERR, "write_lockdata failed in release_lock\n");
		exit(EXIT_FAILURE);
	}
	cl_log(LOG_INFO, "lock released\n");
}

static void quit_handler(int signo, siginfo_t *info, void *context)
{
	cl_log(LOG_INFO, "quit_handler called. now releasing lock\n");
	release_lock();
	cl_log(LOG_INFO, "Shutdown sfex_daemon with EXIT_SUCCESS\n");
	exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{	

	int ret;

	progname = get_progname(argv[0]);
	nodename = get_nodename();

	cl_log_set_entity(progname);
	cl_log_set_facility(HA_LOG_FACILITY);
	cl_inherit_logging_environment(0);

	/* read command line option */
	opterr = 0;
	while (1) {
		int c = getopt(argc, argv, "hi:c:t:m:n:r:");
		if (c == -1)
			break;
		switch (c) {
			case 'h':           /* help*/
				usage(stdout);
				exit(EXIT_SUCCESS);
			case 'i':           /* -i <index> */
				{
					unsigned long l = strtoul(optarg, NULL, 10);
					if (l < SFEX_MIN_NUMLOCKS || l > SFEX_MAX_NUMLOCKS) {
						cl_log(LOG_ERR, 
								"index %s is out of range or invalid. it must be integer value between %lu and %lu.\n",
								optarg,
								(unsigned long)SFEX_MIN_NUMLOCKS,
								(unsigned long)SFEX_MAX_NUMLOCKS);
						exit(4);
					}
					lock_index = l;
				}
				break;
			case 'c':           /* -c <collision_timeout> */
				{
					unsigned long l = strtoul(optarg, NULL, 10);
					if (l < 1 || l > INT_MAX) {
						cl_log(LOG_ERR, 
								"collision_timeout %s is out of range or invalid. it must be integer value between %lu and %lu.\n",
								optarg,
								(unsigned long)1,
								(unsigned long)INT_MAX);
						exit(4);
					}
					collision_timeout = l;
				}
				break;
			case 'm':  			/* -m <monitor_interval> */
				{
					unsigned long l = strtoul(optarg, NULL, 10);
					if (l < 1 || l > INT_MAX) {
						cl_log(LOG_ERR, 
								"monitor_interval %s is out of range or invalid. it must be integer value between %lu and %lu.\n",
								optarg,
								(unsigned long)1,
								(unsigned long)INT_MAX);
						exit(4);
					}
					monitor_interval = l;
				}
				break;	
			case 't':           /* -t <lock_timeout> */
				{
					unsigned long l = strtoul(optarg, NULL, 10);
					if (l < 1 || l > INT_MAX) {
						cl_log(LOG_ERR, 
								"lock_timeout %s is out of range or invalid. it must be integer value between %lu and %lu.\n",
								optarg,
								(unsigned long)1,
								(unsigned long)INT_MAX);
						exit(4);
					}
					lock_timeout = l;
				}
				break;
			case 'n':
				{
					free(nodename);
					if (strlen(optarg) > SFEX_MAX_NODENAME) {
						cl_log(LOG_ERR, "nodename %s is too long. must be less than %d byte.\n",
								optarg,
								(unsigned int)SFEX_MAX_NODENAME);
						exit(EXIT_FAILURE);
					}
					nodename = strdup(optarg);
				}	
				break;
			case 'r':
				{
					rsc_id = strdup(optarg);
				}
				break;
			case '?':           /* error */
				usage(stderr);
				exit(4);
		}
	}
	/* check parameter except the option */
	if (optind >= argc) {
		cl_log(LOG_ERR, "no device specified.\n");
		usage(stderr);
		exit(EXIT_FAILURE);
	} else if (optind + 1 < argc) {
		cl_log(LOG_ERR, "too many arguments.\n");
		usage(stderr);
		exit(EXIT_FAILURE);
	}
	device = argv[optind];

	prepare_lock(device);
#if !SFEX_TESTING
	sysrq_fd = open("/proc/sysrq-trigger", O_WRONLY);
	if (sysrq_fd == -1) {
		cl_log(LOG_ERR, "failed to open /proc/sysrq-trigger due to %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
#endif

	ret = lock_index_check(&cdata, lock_index);
	if (ret == -1)
		exit(EXIT_FAILURE);

	{
		struct sigaction sig_act;
		sigemptyset (&sig_act.sa_mask);
		sig_act.sa_flags = SA_SIGINFO;

		sig_act.sa_sigaction = quit_handler;
		ret = sigaction(SIGTERM, &sig_act, NULL);
		if (ret == -1) {
			cl_log(LOG_ERR, "sigaction failed\n");
			exit(EXIT_FAILURE);
		}
	}

	cl_log(LOG_INFO, "Starting SFeX Daemon...\n");
	
	/* acquire lock first.*/
	acquire_lock();

	if (daemon(0, 1) != 0) {
		cl_perror("%s::%d: daemon() failed.", __FUNCTION__, __LINE__);
		release_lock();
		exit(EXIT_FAILURE);
	}

	cl_make_realtime(-1, -1, 128, 128);
	
	cl_log(LOG_INFO, "SFeX Daemon started.\n");
	while (1) {
		sleep (monitor_interval);
		update_lock();
	}
}
