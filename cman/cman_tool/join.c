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

#include <stdint.h>
#include <signal.h>
#include "libcman.h"
#include "cman_tool.h"

static char *argv[128];
static char *envp[128];

int join(commandline_t *comline)
{
	int i;
	int envptr = 0;
	char scratch[1024];
	cman_handle_t h;
	pid_t aisexec_pid;

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
		snprintf(scratch, sizeof(scratch), "CMAN_MCAST_ADDR=%s", comline->clustername);
		envp[envptr++] = strdup(scratch);
	}
	if (comline->nodenames[0]) {
		snprintf(scratch, sizeof(scratch), "CMAN_NODENAME=%s", comline->nodenames[0]);
		envp[envptr++] = strdup(scratch);
	}
	envp[envptr++] = NULL;

	argv[0] = "aisexec";
	if (comline->verbose)
                argv[1] = "-d";

	if (comline->verbose && comline->wait_opt)
                argv[2] = "-w";

	/* Fork/exec cman */
	switch ( (aisexec_pid = fork()) )
	{
	case -1:
		die("fork of aisexec daemon failed: %s", strerror(errno));

	case 0: // child
		setsid();
		chdir(SBINDIR);
		execve("./aisexec", argv, envp);
		die("execve of " SBINDIR "/aisexec failed: %s", strerror(errno));
		break;

	default: //parent
		break;

	}

#ifdef DEBUG
	if (getenv("DEBUG_WAIT"))
	{
		printf("Waiting to attach gdb to aisexec (pid %d), press ENTER to continue\n", aisexec_pid);
		getchar();
	}
#endif
	/* Give the daemon a chance to start up */
	i = 0;
	do {
		sleep(2);
		h = cman_admin_init(NULL);
		if (!h && comline->verbose)
		{
			fprintf(stderr, "waiting for aisexec to start\n");
		}
	} while (!h && ++i < 20);

	if (!h)
		die("aisexec daemon didn't start");

	if (comline->verbose && !cman_is_active(h))
		fprintf(stderr, "aisexec started, but not joined the cluster yet.\n");

	cman_finish(h);
	return 0;
}
