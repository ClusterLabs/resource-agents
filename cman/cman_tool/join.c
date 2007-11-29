/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <sys/wait.h>
#include <stdint.h>
#include <signal.h>
#include <netinet/in.h>
#include "libcman.h"
#include "cman_tool.h"
#include "ccs.h"

static char *argv[128];
static char *envp[128];

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


int join(commandline_t *comline)
{
	int i;
	int envptr = 0;
	char scratch[1024];
	cman_handle_t h;
	pid_t aisexec_pid;
	int ctree;
	int p[2];

	if (!comline->noccs_opt)
	{
		ctree = ccs_force_connect(NULL, 1);
		if (ctree < 0)
		{
			die("ccsd is not running\n");
		}
		ccs_disconnect(ctree);
	}

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
	if (comline->verbose) {
		snprintf(scratch, sizeof(scratch), "CMAN_DEBUGLOG=%d", comline->verbose);
		envp[envptr++] = strdup(scratch);
	}
	if (comline->noccs_opt) {
		snprintf(scratch, sizeof(scratch), "CMAN_NOCCS=TRUE");
		envp[envptr++] = strdup(scratch);
	}

	/* Use cman to configure services */
	envp[envptr++] = strdup("OPENAIS_DEFAULT_CONFIG_IFACE=cmanconfig");

	/* Create a pipe to monitor cman startup progress */
	pipe(p);
	fcntl(p[1], F_SETFD, 0); /* Don't close on exec */
	snprintf(scratch, sizeof(scratch), "CMAN_PIPE=%d", p[1]);
	envp[envptr++] = strdup(scratch);

	envp[envptr++] = NULL;

	argv[0] = "aisexec";

	/* Fork/exec cman */
	switch ( (aisexec_pid = fork()) )
	{
	case -1:
		die("fork of aisexec daemon failed: %s", strerror(errno));

	case 0: // child
		close(p[0]);
		be_daemon(!comline->verbose);
		execve(AISEXECBIN, argv, envp);

		// exec failed - tell the parent process */
		sprintf(scratch, "execve of " AISEXECBIN " failed: %s", strerror(errno));
		write(p[1], scratch, strlen(scratch));
		exit(1);
		break;

	default: //parent
		break;

	}

	/* Give the daemon a chance to start up, and monitor the pipe FD for messages */
	i = 0;
	close(p[1]);
	do {
		fd_set fds;
		struct timeval tv={1, 0};
		int status;
		char message[1024];

		FD_ZERO(&fds);
		FD_SET(p[0], &fds);

		status = select(p[0]+1, &fds, NULL, NULL, &tv);

		/* Did we get an error? */
		if (status == 1) {
			if (read(p[0], message, sizeof(message)) != 0) {
				fprintf(stderr, "cman not started: %s\n", message);
				break;
			}
			else {
				int pidstatus;
				if (waitpid(aisexec_pid, &pidstatus, WNOHANG) == 0 && pidstatus != 0)
					fprintf(stderr, "cman died with status: %d\n", WEXITSTATUS(pidstatus));
				else
					status = 0; /* Try to connect */
			}
		}
		if (status == 0) {
			h = cman_admin_init(NULL);
			if (!h && comline->verbose)
			{
				fprintf(stderr, "waiting for aisexec to start\n");
			}
		}
	} while (!h && ++i < 100);

	if (!h)
		die("aisexec daemon didn't start");

	if (comline->verbose && !cman_is_active(h))
		fprintf(stderr, "aisexec started, but not joined the cluster yet.\n");

	cman_finish(h);
	return 0;
}
