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

static char *default_mcast(commandline_t *comline)
{
        struct addrinfo *ainfo;
        struct addrinfo ahints;
	int ret;

        memset(&ahints, 0, sizeof(ahints));

        /* Lookup the the nodename address and use it's IP type to
	   default a multicast address */
        ret = getaddrinfo(comline->nodenames[0], NULL, &ahints, &ainfo);
	if (ret)
		return NULL;

	if (ainfo->ai_family == AF_INET)
		return "224.0.9.1";
	if (ainfo->ai_family == AF_INET6)
		return "FF15::1";

	return NULL;
}

int join(commandline_t *comline)
{
	cman_join_info_t join_info;
	int error, i;
	cman_handle_t h;
	char nodename[256];
	char *mcast_name;
	pid_t cmand_pid;

	/*
	 * If we can talk to cman then we're already joined (or joining);
	 */
	h = cman_admin_init(NULL);
	if (h)
		die("Node is already active");

	argv[0] = "cmand";
	if (comline->verbose)
                argv[1] = "-d";

	if (comline->verbose && comline->wait_opt)
                argv[2] = "-w";

	/* Fork/exec cman */
	switch ( (cmand_pid = fork()) )
	{
	case -1:
		die("fork cman daemon failed: %s", strerror(errno));

	case 0: // child
		setsid();
		execve(SBINDIR "/cmand", argv, NULL);
		die("execve of " SBINDIR "/cmand failed: %s", strerror(errno));
		break;

	default: //parent
		break;

	}
#ifdef DEBUG
	if (getenv("DEBUG_WAIT"))
	{
		printf("Waiting to attach gdb to cmand (pid %d), press ENTER to continue\n", cmand_pid);
		getchar();
	}
#endif
	/* Give the daemon a chance to start up */
	i = 0;
	do {
		sleep(1);
		h = cman_admin_init(NULL);
		if (!h && comline->verbose)
		{
			fprintf(stderr, "waiting for cman to start\n");
		}
	} while (!h && ++i < 10);

	if (!h)
		die("cman daemon didn't start");


	/* Set the node name */
	strcpy(nodename, comline->nodenames[0]);

	if (comline->override_nodename)
		error = cman_set_nodename(h, comline->override_nodename);
	else
		error = cman_set_nodename(h, nodename);
	if (error)
	{
		kill(cmand_pid, SIGKILL);
		die("Unable to set cluster node name: %s", cman_error(errno));
	}

	/* Optional, set the node ID */
	if (comline->nodeid)
	{
		error = cman_set_nodeid(h, comline->nodeid);
		if (error)
		{
			kill(cmand_pid, SIGKILL);
			die("Unable to set cluster nodeid: %s", cman_error(errno));
		}
	}

	if (!comline->nodeid)
		die("TEMPORARY: node IDs must be statically assigned at the moment");
	/*
	 * Setup the interface/multicast
	 */
	for (i = 0; i<comline->num_nodenames; i++)
	{
		error = cman_set_interface(h, comline->nodenames[i]);
		if (error)
		{
			kill(cmand_pid, SIGKILL);
			die("Unable to add interface for %s: %s", comline->nodenames[i], cman_error(errno));
		}
	}

	if (comline->multicast_addr)
		mcast_name = comline->multicast_addr;
	else
		mcast_name = default_mcast(comline);

	if (!mcast_name)
		die("Cannot determine a default multicast address");

	error = cman_set_mcast(h, mcast_name);
	if (error)
	{
		kill(cmand_pid, SIGKILL);
		die("Unable to set multicast address %s: %s", mcast_name, cman_error(errno));
	}

        /*
	 * Join cluster
	 */
	join_info.ji_votes = comline->votes;
	join_info.ji_expected_votes = comline->expected_votes;
	strcpy(join_info.ji_cluster_name, comline->clustername);
	join_info.ji_two_node = comline->two_node;
	join_info.ji_config_version = comline->config_version;
	join_info.ji_port = comline->port;

	if (comline->key_filename)
	{
		if (cman_set_commskey(h, comline->key_filename))
		{
			kill(cmand_pid, SIGKILL);
			die("Error loading comms key file %s: %s\n", comline->key_filename, strerror(errno));
		}
	}

	if (cman_join_cluster(h, &join_info))
	{
		kill(cmand_pid, SIGKILL);
		die("error joining cluster: %s", cman_error(errno));
	}

	cman_finish(h);
	return 0;
}
