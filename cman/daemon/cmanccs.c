/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <netinet/in.h>
#include <syslog.h>

#include "logging.h"
#include "totemip.h"
#include "commands.h"
#include "ccs.h"

#define CONFIG_VERSION_PATH	"/cluster/@config_version"
#define EXP_VOTES_PATH		"/cluster/cman/@expected_votes"
#define NODE_NAME_PATH		"/cluster/clusternodes/clusternode[%d]/@name"
#define NODE_VOTES_PATH		"/cluster/clusternodes/clusternode[@name=\"%s\"]/@votes"
#define NODE_NODEID_PATH	"/cluster/clusternodes/clusternode[@name=\"%s\"]/@nodeid"

extern int config_version;

/* Get all the cluster node names from CCS and
   add them to our node list.
   Called when we start up and on "cman_tool version".
*/
int read_ccs_nodes()
{
    int ctree;
    char *nodename;
    char *str;
    int error;
    int i;
    int expected = 0;
    int config;

    /* Open the config file */
    ctree = ccs_force_connect(NULL, 1);
    if (ctree < 0)
    {
	    log_msg(LOG_ERR, "Error connecting to CCS");
	    return -1;
    }

    /* New config version */
    if (!ccs_get(ctree, CONFIG_VERSION_PATH, &str)) {
	    config = atoi(str);
	    free(str);

	    /* config_version is zero at startup when we read initial config */
	    if (config_version && config != config_version) {
		    ccs_disconnect(ctree);
		    log_msg(LOG_ERR, "CCS version is %d, we expected %d. config not updated\n",
			    config, config_version);
		    return -1;
	    }
	    config_version = config;
    }

    /* This overrides any other expected votes calculation /except/ for
       one specified on a join command-line at join time */
    if (!ccs_get(ctree, EXP_VOTES_PATH, &str)) {
	    expected = atoi(str);
	    free(str);
    }

    for (i=1;;i++)
    {
	char nodekey[256];
	char key[256];
	int  votes=0, nodeid=0;

	sprintf(nodekey, NODE_NAME_PATH, i);
	error = ccs_get(ctree, nodekey, &nodename);
	if (error)
	    break;

	sprintf(key, NODE_VOTES_PATH, nodename);
	if (!ccs_get(ctree, key, &str))
	{
	    votes = atoi(str);
	    free(str);
	}

	sprintf(key, NODE_NODEID_PATH, nodename);
	if (!ccs_get(ctree, key, &str))
	{
	    nodeid = atoi(str);
	    free(str);
	}

	// TODO get altnames for the node

	P_MEMB("Got node %s from ccs (id=%d, votes=%d)\n", nodename, nodeid, votes);
	add_ccs_node(nodename, nodeid, votes, expected);

	free(nodename);
    }

    /* Finished with config file */
    ccs_disconnect(ctree);

    return 0;
}
