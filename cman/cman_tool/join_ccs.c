/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <stdint.h>

#include "ccs.h"
#include "cnxman-socket.h"
#include "cman_tool.h"


#define CLUSTER_NAME_PATH	"//cluster/@name"
#define CONFIG_VERSION_PATH	"//cluster/@config_version"
#define EXP_VOTES_PATH		"//cluster/cman/@expected_votes"
#define TWO_NODE_PATH		"//cluster/cman/@two_node"
#define MCAST_ADDR_PATH		"//cluster/cman/multicast/@addr"
#define PORT_PATH		"//cluster/cman/@port"

#define NODEI_NAME_PATH		"//cluster/nodes/node[%d]/@name"
#define NODE_NAME_PATH		"//cluster/nodes/node[@name=\"%s\"]/@name"
#define NODE_VOTES_PATH		"//cluster/nodes/node[@name=\"%s\"]/@votes"
#define NODE_IFNAME_PATH        "//cluster/nodes/node[@name=\"%s\"]/@ifname"
#define NODE_ALTNAMES_PATH	"//cluster/nodes/node[@name=\"%s\"]/altname/@name"
#define NODE_MCAST_IF_PATH	"//cluster/nodes/node[@name=\"%s\"]/multicast[@addr=\"%s\"]/@interface"



int get_ccs_join_info(commandline_t *comline)
{
	char path[MAX_PATH_LEN];
	char nodename[MAX_NODE_NAME_LEN];
	char *str, *name, *cname = NULL;
	int cd, error, i, vote_sum = 0, node_count = 0;

	if (comline->clustername[0])
		cname = comline->clustername;

	cd = ccs_force_connect(cname, 1);
	if (cd < 0)
		die("cannot connect to ccs (name=%s)",
		    cname ? comline->clustername : "none");


	memset(&nodename, 0, MAX_NODE_NAME_LEN);
	error = uname_to_nodename(nodename);
	if (error)
		die("cannot get local node name from uname");

	/* cluster name */

	error = ccs_get(cd, CLUSTER_NAME_PATH, &str);
	if (!error) {
		strcpy(comline->clustername, str);
		free(str);
	}


	/* config version */

	error = ccs_get(cd, CONFIG_VERSION_PATH, &str);
	if (!error) {
		comline->config_version = atoi(str);
		free(str);
	}


	/* sum node votes for expected */

	for (i = 1; ; i++) {
		name = NULL;
		memset(path, 0, MAX_PATH_LEN);
		sprintf(path, NODEI_NAME_PATH, i);

		error = ccs_get(cd, path, &name);
		if (error || !name)
			break;

		node_count++;

		memset(path, 0, MAX_PATH_LEN);
		sprintf(path, NODE_VOTES_PATH, name);
		free(name);

		error = ccs_get(cd, path, &str);
		if (error)
			vote_sum++;
		else {
			vote_sum += atoi(str);
			free(str);
		}
	}


	/* optional expected_votes supercedes vote sum */

	error = ccs_get(cd, EXP_VOTES_PATH, &str);
	if (!error) {
		comline->expected_votes = atoi(str);
		free(str);
	} else
		comline->expected_votes = vote_sum;


	/* optional port */

	error = ccs_get(cd, PORT_PATH, &str);
	if (!error) {
		comline->port = atoi(str);
		free(str);
		if (comline->verbose)
		    printf("Got port number %d\n", comline->port);
	}


	/* optional multicast name(s) with interfaces */
	i=0;
	do {
	    str = NULL;
	    error = ccs_get(cd, MCAST_ADDR_PATH, &str);
	    if (!error && str) {
		/* If we get the same thing twice, it's probably the end of a
		 * 1-element list */
		if (i>0 && strcmp(str, comline->multicast_names[i-1]) == 0) {
			free(str);
			break;
		}
		comline->multicast_names[i++] = str;

		if (comline->verbose)
		    printf("Got Multicast address %s\n", str);
	    }

	} while (str);

	comline->num_multicasts = i;
	for (i=0; i<comline->num_multicasts; i++) {
	    sprintf(path, NODE_MCAST_IF_PATH, nodename, comline->multicast_names[i]);
	    error = ccs_get(cd, path, &str);
	    if (!error) {
		comline->interfaces[i] = str;

		if (comline->verbose)
		    printf("Got Interface %s for multicast %s\n", str, comline->multicast_names[i]);

	    }
	    else
		die("No interface for multicast address %s\n", comline->multicast_names[i]);
	}
	comline->num_interfaces = comline->num_multicasts;

	/* find our own number of votes */

	memset(path, 0, MAX_PATH_LEN);
	sprintf(path, NODE_NAME_PATH, nodename);

	error = ccs_get(cd, path, &str);
	if (error)
		die("cannot find local node name \"%s\" in ccs", nodename);
	free(str);

	memset(path, 0, MAX_PATH_LEN);
	sprintf(path, NODE_VOTES_PATH, nodename);

	error = ccs_get(cd, path, &str);
	if (!error) {
		comline->votes = atoi(str);
		free(str);
	}

	/* Get all alternative node names */
	comline->nodenames[0] = strdup(nodename);
	sprintf(path, NODE_ALTNAMES_PATH, nodename);
	i=1;
	do {
	    str = NULL;
	    error = ccs_get(cd, path, &str);
	    if (!error && str) {
		/* If we get the same thing twice, it's probably the end of a
		 * 1-element list */
		if (i>0 && strcmp(str, comline->nodenames[i-1]) == 0) {
			free(str);
			break;
		}
		comline->nodenames[i++] = str;

		if (comline->verbose)
		    printf("Got alternative node name %s\n", str);
	    }

	} while (str);
	comline->num_nodenames = i;


	/* two_node */

	error = ccs_get(cd, TWO_NODE_PATH, &str);
	if (!error) {
		comline->two_node = atoi(str);
		free(str);
		if (comline->two_node) {
			if (node_count != 2 || vote_sum != 2 ||
			    comline->votes != 1 || comline->expected_votes != 1)
				die("the two-node option requires exactly two "
			    	    "nodes with one vote each and expected "
				    "votes set to 1");
		}
	}

	ccs_disconnect(cd);
	return 0;
}
