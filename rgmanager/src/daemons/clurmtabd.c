/** @file
 * Keeps /var/lib/nfs/rmtab in sync across the cluster.
 *
 * Author: Lon H. Hohberger <lhh at redhat.com>
 *
 * Synchronizes entries in a mount point with /var/lib/nfs/rmtab.
 */

#define CM_NFS_DIR ".clumanager"
 
#include <stdio.h>
#include <rmtab.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <syslog.h>
#include <clulog.h>
#include <unistd.h>
#include <limits.h>
#include <regex.h>

#define POLLINT_DEFAULT 2

/* FIXME DAEMON_STR is equivalent to the one in quorumd.c: CLURMTABD_DAEMON */
/* Would be nice to have stuff like this defined in one place */
#define DAEMON_STR	"clurmtabd"
#define LOGLEVEL_STR	DAEMON_STR "%logLevel"
#define POLLINT_STR	DAEMON_STR "%pollInterval"

/*
 * Globals
 */

static int exiting = 0;
static int poll_interval = POLLINT_DEFAULT;

/*
 * Function Prototypes
 */
static int rmtab_modified(void);
static int rmtab_copy_bypath(rmtab_node ** dest, rmtab_node ** src,
			     const char *path);
static int rmtab_get_update(rmtab_node ** rmtab, rmtab_node ** pruned_rmtab,
			    rmtab_node ** diff, char *path);

/* Signal Handlers */
static void sh_sync(int sig);
static void sh_exit(int sig);
static void sh_reconfigure(int sig);
static inline void register_sighandlers(void);

/* Configuration */
#if 0
static inline int __get_int_param(char *str, int *val, int dflt);
#endif
static int get_rmtabd_loglevel(void);
static int get_rmtabd_pollinterval(int *interval);
static void rmtabd_reconfigure(void);

/* Initialization */
static int rmtabd_config_init(void);
int main(int argc, char **argv);


/**
 * stat _PATH_RMTAB and see if it's changed.
 *
 * @returns		1 if it's been modified; 0 if not.
 */
static int
rmtab_modified(void)
{
	/* Preserved data */
	static struct stat prev_stat;
	static int __prev_stat = 0;

	struct stat curr_stat;
	int rv = 1;

	/* Initialize */
	if (!__prev_stat) {
		memset(&prev_stat, 0, sizeof (prev_stat));
		stat(_PATH_RMTAB, &prev_stat);
		__prev_stat = 1;
		return 1;
	}

	memset(&curr_stat, 0, sizeof (curr_stat));
	while (stat(_PATH_RMTAB, &curr_stat) == -1) {
		if (errno != ENOENT) {
			clulog(LOG_ERR, "#15: %s: stat: %s\n", __FUNCTION__,
			       strerror(errno));
			return -1;
		}

		/* Create the file. */
		clulog(LOG_WARNING, "#62: " _PATH_RMTAB
		       " does not exist - creating");
		close(open(_PATH_RMTAB, O_CREAT | O_SYNC, 0600));
	}

	if ((rv = memcmp(&prev_stat.st_mtime, &curr_stat.st_mtime,
			 sizeof (curr_stat.st_mtime)))) {
		clulog(LOG_DEBUG, "Detected modified " _PATH_RMTAB "\n");
		memcpy(&prev_stat, &curr_stat, sizeof (prev_stat));
	}

	return !!rv;
}


/**
 * Insert (copy) entries in **src with the same rn_path as *path to
 * destination list **dest.
 *
 * @param dest		Destination list pointer.
 * @param src		Source list pointer.
 * @param path		Path to prune (only copy entries in the specified
 *			path).
 * Returns -1 if rmtab_insert fails; 0 on success
 */
static int
rmtab_copy_bypath(rmtab_node ** dest, rmtab_node ** src, const char *path)
{
	rmtab_node *curr, *last = NULL;

	for (curr = *src; curr; curr = curr->rn_next)
		if (!strcmp(path, curr->rn_path))
			if ((last = rmtab_insert(dest, last,
						curr->rn_hostname,
						curr->rn_path,
						curr->rn_count)) == NULL)
				return -1;
	return 0;
}


/**
 * Update the rmtab from /var/lib/nfs/rmtab.  We need to maintain two separate
 * lists (rmtab and pruned_rmtab).  This is because we don't want to sync
 * non-cluster exports.  Non-cluster exports will not show up in pruned_rmtab,
 * however, when we receive an update from our peer, we'd lose non-cluster
 * entries if we didn't preserve them when we merge in changes from our peer.
 *
 * The current (full) rmtab is passed in as **rmtab.  The current cluster-only
 * (pruned) rmtab is passed in as **pruned_rmtab.  The differences between
 * the current and new cluster-only rmtabs are passed out in **diff, and the
 * new versions (if any) of the full rmtab and pruned rmtab are moved into
 *
 * @return		-1 on error, 0 on success, 1 if no differences exist.
 */
static int
rmtab_get_update(rmtab_node ** rmtab, rmtab_node ** pruned_rmtab,
		 rmtab_node ** diff, char *path)
{
	int rv = -1;
	rmtab_node *old_rmtab = NULL, *old_pruned = NULL;

	if (!rmtab_modified())
		return 1;

	/* Save the current full list */
	rmtab_move(&old_rmtab, rmtab);

	if (rmtab_read(rmtab, _PATH_RMTAB) == -1) {
		clulog(LOG_ERR, "#16: Failed to reread rmtab: %s\n",
		       strerror(errno));

		/* Don't kill the list if we fail to reread. */
		rmtab_move(rmtab, &old_rmtab);
		goto out;
	}

	/* Save the current cluster-specific list */
	rmtab_move(&old_pruned, pruned_rmtab);

	if (rmtab_copy_bypath(pruned_rmtab, rmtab, path) == -1) {
		clulog(LOG_ERR, "#17: Failed to prune rmtab: %s\n",
		       strerror(errno));

		/* 
		 * Since we couldn't build a new list, restore the old
		 * one.  Otherwise, next time, we'd send a weird diff to
		 * our peer with all entries as "added".
		 */
		rmtab_move(pruned_rmtab, &old_pruned);
		goto out;
	}

	if (!diff) {
		rv = 1;
		goto out;
	}

	/* find the differences */
	if (rmtab_diff(old_pruned, *pruned_rmtab, diff)) {
		clulog(LOG_ERR, "Failed to diff rmtab: %s\n", strerror(errno));
		goto out;
	}

	if (!*diff) {
		/* No differences */
		rv = 1;
		goto out;
	}

	rv = 0;
      out:
	/* stick a finger in the memory dike */
	rmtab_kill(&old_rmtab);	/* these will be NOPs if NULL */
	rmtab_kill(&old_pruned);

	return rv;
}




/* **************** *
 * SIGNAL HANDLERS!
 * **************** */

/**
 * INT, USR1, USR2 handler.
 *
 * What these signals actually do is interrupt the select(2) we enter when we
 * call sleep().  Effectively, this causes sleep() to short out, and makes 
 * us drop down into rmtab_get_update() - causing us to re-check and sync
 * changes if there are any.  The service script, svclib_nfs, sends us a
 * TERM whenever it receives the request to stop a service (which happens
 * when the service manager relocates as well), thus, when a service is
 * disabled or relocates to the other node, clurmtabd syncs immediately its
 * current state to the other node, preventing a timing window between
 * "service relocate" and "rmtabd update" during which a client could
 * receive ESTALE.
 */
static void
sh_sync(int sig)
{
	clulog(LOG_DEBUG, "Signal %d received; syncing ASAP\n", sig);
}


/**
 * QUIT, TERM
 *
 * In this case, we go down ASAP.  But first, we sync.  These will, like the
 * above, short-out msg_accept_timeout() and drop down for that one last
 * sync.
 */
static void
sh_exit(int sig)       
{
	clulog(LOG_DEBUG, "Signal %d received; exiting\n", sig);
	exiting = 1;
}


/**
 * HUP
 *
 * Traditional behavior.  Reconfigure on SIGHUP.
 */
static void
sh_reconfigure(int __attribute__ ((unused)) sig)
{
	clulog(LOG_DEBUG, "Re-reading the cluster database\n");
	rmtabd_reconfigure();
}


/**
 * Set up signal handlers.
 */
static inline void
register_sighandlers(void)
{
	sigset_t set;
	struct sigaction act;

	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGUSR1);
	sigaddset(&set, SIGUSR2);

	sigaddset(&set, SIGHUP);

	sigaddset(&set, SIGTERM);
	sigaddset(&set, SIGQUIT);

	sigaddset(&set, SIGILL);
	sigaddset(&set, SIGIO);
	sigaddset(&set, SIGSEGV);
	sigaddset(&set, SIGBUS);

	sigprocmask(SIG_UNBLOCK, &set, NULL);

	memset(&act, 0, sizeof (act));
	sigemptyset(&act.sa_mask);

	/* In some cases, just continue */
	act.sa_handler = sh_sync;

	sigaction(SIGINT, &act, NULL);
	sigaction(SIGUSR1, &act, NULL);
	sigaction(SIGUSR2, &act, NULL);

	/* Ok, reconfigure here */
	act.sa_handler = sh_reconfigure;
	sigaction(SIGHUP, &act, NULL);

	/* Exit signals */
	act.sa_handler = sh_exit;
	sigaction(SIGTERM, &act, NULL);
	sigaction(SIGQUIT, &act, NULL);
}

/* ******************************* *
 * Configuration Utility Functions
 * ******************************* */

/**
 * Retrieve an integer parameter from the config file.
 *
 * @param str		config token
 * @param val		return value
 * @param dflt		Default integer value.
 * @return 0                                                      s
 */
#if 0
static inline int
__get_int_param(char *str, int *val, int dflt)
{
	char *value;
	int ret;

	ret = CFG_Get(str, NULL, &value);

	switch (ret) {
	case CFG_DEFAULT:
		*val = dflt;
		break;
	case CFG_OK:
		*val = atoi(value);
		break;
	default:
		clulog(LOG_ERR, "#19: Cannot get \"%s\" from database; "
		       "CFG_Get() failed, err=%d\n", ret);
		return 0;
	}

	return 0;
}
#endif


/**
 * Gets the loglevel of rmtabd
 */
static int
get_rmtabd_loglevel(void)
{
#if 0
	return __get_int_param(LOGLEVEL_STR, level, LOG_DEFAULT);
#endif
	return LOG_INFO;
}


/**
 * Retrieves the polling interval, in seconds, of _RMTAB_PATH from the cluster
 * configuration database.
 */
static int
get_rmtabd_pollinterval(int __attribute__((unused)) *interval)
{
#if 0
	return __get_int_param(POLLINT_STR, interval, POLLINT_DEFAULT);
#endif
	return POLLINT_DEFAULT;
}


/**
 * This is called at init and by sh_reconfigure and sets up daemon-specific
 * configuration params.
 */
static void
rmtabd_reconfigure(void)
{
	int level, old_level, old_interval;

	/* loglevel */
	old_level = clu_get_loglevel();
	level = get_rmtabd_loglevel();

	if (old_level != level) {
		if (clu_set_loglevel(level) == -1)
			clulog(LOG_ERR, "#20: Failed set log level\n");
		else
			clulog(LOG_DEBUG, "Log level is now %d\n", level);
	}

	/* rmtabd polling interval (tw33k4bl3) */
	old_interval = poll_interval;
	get_rmtabd_pollinterval(&poll_interval);

	/* bounds-check */
	if (poll_interval < 1)
		poll_interval = 1;
	else if (poll_interval > 10)
		poll_interval = 10;

	if (old_interval != poll_interval) {
		clulog_and_print(LOG_DEBUG,
				 "Polling interval is now %d seconds\n",
				 poll_interval);
	}
}


/**
 * Set up local parameters & signal handlers.
 */
static int
rmtabd_config_init(void)
{
	/* Yes, it does this twice */
#if 0
	if (CFG_ReadFile(CLU_CONFIG_FILE) != CFG_OK)
		return -1;
#endif

	rmtabd_reconfigure();
	register_sighandlers();
	return 0;
}


/**
 * Initializes and synchronizes /var/lib/nfs/rmtab with
 * [path]/.clumanager/rmtab.
 *
 * @param path		Path to mount point we're monitoring.
 * @param rmtab		Will contain full rmtab upon exit.
 * @param pruned_rmtab	Will contain rmtab entries we care about on exit.
 */
int
rmtab_init(char *path, rmtab_node **rmtab, rmtab_node **pruned_rmtab)
{
	char buf[PATH_MAX];

	snprintf(buf, sizeof(buf), "%s/%s", path, CM_NFS_DIR);

	if ((mkdir(buf, 0700) == -1) && (errno != EEXIST)) {
		clulog_and_print(LOG_ERR, "#21: Couldn't read/create %s: %s\n",
				 buf, strerror(errno));
		return -1;
	}

	snprintf(buf, sizeof(buf), "%s/%s/rmtab", path, CM_NFS_DIR);

	if (rmtab_read(rmtab, buf) == -1) {
		clulog_and_print(LOG_ERR, "#22: Failed to read %s: %s\n", buf,
				 strerror(errno));
		return -1;
	}

	/*
	 * Read into the same pointer -> inserting each node will
	 * cause the nodes with the greater count to be kept.
	 */
	if (rmtab_read(rmtab, _PATH_RMTAB) == -1) {
		clulog_and_print(LOG_ERR, "#23: Failed to read %s: %s\n",
				 _PATH_RMTAB, strerror(errno));
		return -1;
	}

	/*
	 * Prune by our path
	 */
	if (rmtab_copy_bypath(pruned_rmtab, rmtab, path) == -1) {
		clulog_and_print(LOG_ERR, "#24: Failed to prune rmtab: %s\n",
				 strerror(errno));
		return -1;
	}

	/*
	 * XXX could lose a mount if rpc.mountd writes a file before
	 * we rewrite the file.
	 */
	if (rmtab_write_atomic(*rmtab, _PATH_RMTAB) == -1) {
		clulog_and_print(LOG_ERR, "#25: Failed to write %s: %s\n",
				 _PATH_RMTAB, strerror(errno));
		return -1;
	}
	/*
	 * Write new contents.
	 */
	if (rmtab_write_atomic(*pruned_rmtab, buf) == -1) {
		clulog_and_print(LOG_ERR, "#26: Failed to write %s: %s\n", buf,
				 strerror(errno));
		return -1;
	}
	
	return 0;
}


/**
 * Fork off into the background and store our pid file in
 * [path]/.clumanager/pid
 *
 * @param path		Mount point we're monitoring.
 * @return 		-1 on failure, 0 on success.
 */
static int
daemonize(char *path)
{
	FILE *fp=NULL;
	char filename[PATH_MAX];

	if (daemon(0,0) == -1)
		return -1;

	memset(filename,0,PATH_MAX);
	snprintf(filename, sizeof(filename), "%s/%s/pid", path, CM_NFS_DIR);

	fp = fopen(filename, "w");
	if (fp == NULL) {
		clulog(LOG_WARNING, "#63: Couldn't write PID!\n");
	}

	fprintf(fp, "%d", getpid());
	fclose(fp);

	return 0;
}


/**
 * main
 *
 * Main.  Main.  Main.  Main.  Main.  Main.  Main.  Main.  Main.  Main.  
 */
int
main(int argc, char **argv)
{
	char path[PATH_MAX];
	char rmtab_priv[PATH_MAX];
	
	rmtab_node *rmtab = NULL, *pruned_rmtab = NULL, *diff = NULL;
	
	if (argc < 2) {
		fprintf(stderr, "usage: clurmtabd <mount-point>\n");
		return -1;
	}

	/* Set up configuration parameters */
	if (rmtabd_config_init() == -1) {
		clulog_and_print(LOG_ERR,
			         "#27: Couldn't initialize - exiting\n");
		return -1;
	}

        /* Set up our internal variables */
	snprintf(path, sizeof(path), "%s", argv[1]);
	snprintf(rmtab_priv, sizeof(rmtab_priv), "%s/%s/rmtab", path,
		 CM_NFS_DIR);

	/*
	 * Synchronize the rmtab files
	 *
	 * We do this before we call daemonize() to ensure that when
	 * the service script calls exportfs, /var/lib/nfs/rmtab has
	 * all the necessary entries.
	 */
	if (rmtab_init(path, &rmtab, &pruned_rmtab) == -1) {
		clulog_and_print(LOG_WARNING,
				 "#64: Could not validate %s\n", path);
		clulog_and_print(LOG_WARNING,
				 "#65: NFS Failover of %s will malfunction\n",
				 path);
		return -1;
	}

	/* Jump off into the background */
	if (daemonize(path) == -1) {
		clulog_and_print(LOG_ERR, "#28: daemonize: %s\n",
				 strerror(errno));
		return -1;
	}

	/* Main loop */
	while (!exiting) {

		/* Snooze a bit */
		sleep(poll_interval);

		/* Check for updates */
		if (rmtab_get_update(&rmtab, &pruned_rmtab, &diff, path)
		    == 0) {
			/* Handle updates */
			rmtab_merge(&pruned_rmtab, diff);
			rmtab_kill(&diff);
			if (rmtab_write_atomic(pruned_rmtab, rmtab_priv) == -1)
				clulog(LOG_ERR,
				       "#29: rmtab_write_atomic: %s\n",
				       strerror(errno));
		}
	}

	return 0;
}
