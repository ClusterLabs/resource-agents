#include <sys/wait.h>
#include <stdint.h>
#include <signal.h>
#include <netinet/in.h>
#include "libcman.h"
#include "cman_tool.h"

#define MAX_ARGS 128

static char *argv[MAX_ARGS];
static char *envp[MAX_ARGS];

static void be_daemon(int close_stderr)
{
	int devnull = open("/dev/null", O_RDWR);
	if (devnull == -1) {
		perror("Can't open /dev/null");
		exit(3);
	}

	/* Detach ourself from the calling environment */
	if (close(0) || close(1)) {
		die("Error closing terminal FDs");
	}

	if (dup2(devnull, 0) < 0 || dup2(devnull, 1) < 0) {
		die("Error setting terminal FDs to /dev/null: %m");
	}

	if (close_stderr) {
		if (close(2)) {
			die("Error closing stderr FD");
		}
		if (!dup2(devnull, 2) < 0) {
			die("Error setting stderr FD to /dev/null: %m");
		}
	}

	setsid();
}


static char *corosync_exit_reason(signed char status)
{
	static char reason[256];
	switch (status) {
	case -2:
		return "Could not determine UID to run as";
		break;
	case -3:
		return "Could not determine GID to run as";
		break;
	case -4:
		return "Error initialising memory pool";
		break;
	case -5:
		return "Could not fork";
		break;
	case -6:
		return "Could not bind to libais socket";
		break;
	case -7:
		return "Could not bind to network socket";
		break;
	case -8:
		return "Could not read security key for communications";
		break;
	case -9:
		return "Could not read cluster configuration";
		break;
	case -10:
		return "Could not set up logging";
		break;
	case -11:
		return "Could not dynamically load modules";
		break;
	case -12:
		return "Could not load and initialise object database";
		break;
	case -13:
		return "Could not initialise all required services";
		break;
	case -14:
		return "Out of memory";
		break;
	default:
		sprintf(reason, "Error, reason code is %d", status);
		return reason;
		break;
	}
}

static int check_corosync_status(pid_t pid)
{
	int status;
	int pidstatus;

	status = waitpid(pid, &pidstatus, WNOHANG);
	if (status == -1 && errno == ECHILD) {

		return 0;
	}
	if ((status == 0 || status == pid) && pidstatus != 0) {
		if (WIFEXITED(pidstatus))
			fprintf(stderr, "corosync died: %s\n", corosync_exit_reason(WEXITSTATUS(pidstatus)));
		if (WIFSIGNALED(pidstatus))
			fprintf(stderr, "corosync died with signal: %d\n", WTERMSIG(pidstatus));
		exit(1);
	}
	return status;
}

int join(commandline_t *comline, char *main_envp[])
{
	int i, err;
	int envptr = 0;
	int argvptr = 0;
	char scratch[1024];
	cman_handle_t h;
	int status;
	pid_t corosync_pid;
	int p[2];

        /*
	 * If we can talk to cman then we're already joined (or joining);
	 */
	h = cman_admin_init(NULL);
	if (h)
		die("Node is already active");

	/* Set up environment variables for override */
	if (comline->multicast_addr) {
		snprintf(scratch, sizeof(scratch), "CMAN_MCAST_ADDR=%s", comline->multicast_addr);
		envp[envptr++] = strdup(scratch);
	}
	if (comline->votes_opt) {
		snprintf(scratch, sizeof(scratch), "CMAN_VOTES=%d", comline->votes);
		envp[envptr++] = strdup(scratch);
	}
	if (comline->expected_votes_opt) {
		snprintf(scratch, sizeof(scratch), "CMAN_EXPECTEDVOTES=%d", comline->expected_votes);
		envp[envptr++] = strdup(scratch);
	}
	if (comline->port) {
		snprintf(scratch, sizeof(scratch), "CMAN_IP_PORT=%d", comline->port);
		envp[envptr++] = strdup(scratch);
	}
	if (comline->nodeid) {
		snprintf(scratch, sizeof(scratch), "CMAN_NODEID=%d", comline->nodeid);
		envp[envptr++] = strdup(scratch);
	}
	if (comline->clustername_opt) {
		snprintf(scratch, sizeof(scratch), "CMAN_CLUSTER_NAME=%s", comline->clustername);
		envp[envptr++] = strdup(scratch);
	}
	if (comline->nodenames[0]) {
		snprintf(scratch, sizeof(scratch), "CMAN_NODENAME=%s", comline->nodenames[0]);
		envp[envptr++] = strdup(scratch);
	}
	if (comline->key_filename) {
		snprintf(scratch, sizeof(scratch), "CMAN_KEYFILE=%s", comline->key_filename);
		envp[envptr++] = strdup(scratch);
	}
	if (comline->two_node) {
		snprintf(scratch, sizeof(scratch), "CMAN_2NODE=true");
		envp[envptr++] = strdup(scratch);
	}
	if (comline->verbose ^ DEBUG_STARTUP_ONLY) {
		snprintf(scratch, sizeof(scratch), "CMAN_DEBUGLOG=%d", comline->verbose);
		envp[envptr++] = strdup(scratch);
	}
	if (comline->noconfig_opt) {
		envp[envptr++] = strdup("CMAN_NOCONFIG=true");
		snprintf(scratch, sizeof(scratch), "COROSYNC_DEFAULT_CONFIG_IFACE=cmanpreconfig%s",
			 comline->noopenais_opt?"":":openaisserviceenable");
		envp[envptr++] = strdup(scratch);
	}
	else {
		snprintf(scratch, sizeof(scratch), "COROSYNC_DEFAULT_CONFIG_IFACE=%s:cmanpreconfig%s", comline->config_lcrso,
			 comline->noopenais_opt?"":":openaisserviceenable");
		envp[envptr++] = strdup(scratch);
	}

	/* Copy any COROSYNC_* env variables to the new daemon */
	i=0;
	while (i < MAX_ARGS && main_envp[i]) {
		if (strncmp(main_envp[i], "COROSYNC_", 9) == 0)
			envp[envptr++] = main_envp[i];
		i++;
	}


	/* Create a pipe to monitor cman startup progress */
	if (pipe(p) < 0)
		die("unable to create pipe: %s", strerror(errno));
	fcntl(p[1], F_SETFD, 0); /* Don't close on exec */
	snprintf(scratch, sizeof(scratch), "CMAN_PIPE=%d", p[1]);
	envp[envptr++] = strdup(scratch);
	envp[envptr++] = NULL;

	/* Always run corosync -f because we have already forked twice anyway, and
	   we want to return any exit code that might happen */
	argv[0] = "corosync";
	argv[++argvptr] = "-f";
	if (comline->nosetpri_opt)
		argv[++argvptr] = "-p";
	argv[++argvptr] = NULL;

	/* Fork/exec cman */
	switch ( (corosync_pid = fork()) )
	{
	case -1:
		die("fork of corosync daemon failed: %s", strerror(errno));

	case 0: /* child */
		close(p[0]);
		if (comline->verbose & DEBUG_STARTUP_ONLY) {
			fprintf(stderr, "Starting %s", COROSYNCBIN);
			for (i=0; i< argvptr; i++) {
				fprintf(stderr, " %s", argv[i]);
			}
			fprintf(stderr, "\n");
			for (i=0; i<envptr-1; i++) {
				fprintf(stderr, "%s\n", envp[i]);
			}
		}
		be_daemon(!(comline->verbose & ~DEBUG_STARTUP_ONLY));

		sprintf(scratch, "FORKED: %d\n", getpid());
		err = write(p[1], scratch, strlen(scratch));

		execve(COROSYNCBIN, argv, envp);

		/* exec failed - tell the parent process */
		sprintf(scratch, "execve of " COROSYNCBIN " failed: %s", strerror(errno));
		err = write(p[1], scratch, strlen(scratch));
		exit(1);
		break;

	default: /* parent */
		break;

	}

	/* Give the daemon a chance to start up, and monitor the pipe FD for messages */
	i = 0;
	close(p[1]);

	/* Wait for the process to start or die */
	sleep(1);
	do {
		fd_set fds;
		struct timeval tv={1, 0};
		char message[1024];
		char *messageptr = message;

		FD_ZERO(&fds);
		FD_SET(p[0], &fds);

		status = select(p[0]+1, &fds, NULL, NULL, &tv);

		/* Did we get a cman-reported error? */
		if (status == 1) {
			int len;
			if ((len = read(p[0], message, sizeof(message)) > 0)) {

				/* Forked OK - get the real corosync pid */
				if (sscanf(messageptr, "FORKED: %d", &corosync_pid) == 1) {
					if (comline->verbose & DEBUG_STARTUP_ONLY)
						fprintf(stderr, "forked process ID is %d\n", corosync_pid);
					status = 1;

					/* There might be a SUCCESS message in the pipe too. */
					messageptr = strchr(messageptr, '\n');
					if (messageptr)
						messageptr++;
					else
						continue;
				}
				/* Success! get the new PID of double-forked corosync */
				if (sscanf(messageptr, "SUCCESS: %d", &corosync_pid) == 1) {
					if (comline->verbose & DEBUG_STARTUP_ONLY)
						fprintf(stderr, "corosync running, process ID is %d\n", corosync_pid);
					status = 0;
					break;
				}
			}
			else if (len < 0 && errno == EINTR) {
				continue;
			}
			else { /* Error or EOF - check the child status */
				status = check_corosync_status(corosync_pid);
			}
		}

	} while (status != 0);
	close(p[0]);

	/* If corosync has started, try to connect to cman ... if it's still there */
	if (status == 0) {
		do {
			if (status == 0) {
				if (kill(corosync_pid, 0) < 0) {
					status = check_corosync_status(corosync_pid);
					die("corosync died during startup\n");
				}

				h = cman_admin_init(NULL);
				if (!h && comline->verbose & DEBUG_STARTUP_ONLY)
				{
					fprintf(stderr, "waiting for cman to start\n");
					status = check_corosync_status(corosync_pid);
				}
			}
			sleep (1);
		} while (!h && ++i < 100);
	}

	if (!h)
		die("corosync daemon didn't start");

	if ((comline->verbose & DEBUG_STARTUP_ONLY) && !cman_is_active(h))
		fprintf(stderr, "corosync started, but not joined the cluster yet.\n");

	cman_finish(h);
	return 0;
}
