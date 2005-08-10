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

#include <net/route.h>
#include "libcman.h"
#include "cman_tool.h"

static char *argv[128];

int join(commandline_t *comline)
{
	cman_join_info_t join_info;
	int error, i;
	cman_handle_t h;
	char nodename[256];
	char *mcast_name;

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
	switch (fork())
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

	/* Give the daemon a chance to start up */
	i = 0;
	do {
		sleep(1);
		h = cman_admin_init(NULL);
		if (!h && comline->verbose) {
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
		die("Unable to set cluster node name: %s", cman_error(errno));

	/* Optional, set the node ID */
	if (comline->nodeid) {
		error = cman_set_nodeid(h, comline->nodeid);
		if (error)
			die("Unable to set cluster nodeid: %s", cman_error(errno));
	}

	/*
	 * Setup the interface/multicast
	 */
	for (i = 0; i<comline->num_nodenames; i++)
	{
		error = cman_set_interface(h, comline->nodenames[i]);
		if (error)
			die("Unable to add interface for %s: %s", comline->nodenames[i], cman_error(errno));
	}

	if (comline->multicast_names[0])
		mcast_name = comline->multicast_names[0];
	else
		mcast_name = "224.0.9.1"; // TODO use something sensible
	error = cman_set_mcast(h, mcast_name);
	if (error)
		die("Unable to set multicast address %s: %s", mcast_name, cman_error(errno));

        /*
	 * Join cluster
	 */
	join_info.ji_votes = comline->votes;
	join_info.ji_expected_votes = comline->expected_votes;
	strcpy(join_info.ji_cluster_name, comline->clustername);
	join_info.ji_two_node = comline->two_node;
	join_info.ji_config_version = comline->config_version;
	join_info.ji_port = comline->port;

	if (cman_join_cluster(h, &join_info))
	{
		die("error joining cluster: %s", cman_error(errno));
	}

	cman_finish(h);
	return 0;
}
