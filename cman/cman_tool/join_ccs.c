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

	if (comline->num_multicasts ||
	    comline->num_nodenames ||
	    comline->config_version ||
	    comline->votes ||
	    comline->expected_votes ||
	    comline->two_node ||
	    comline->port)
		printf("ccs values will override some command line values\n");

	memset(&nodename, 0, MAX_NODE_NAME_LEN);

	/* nodenames[0] may be used as a nodename override from the
	   command-line. else we use uname */
	if (comline->nodenames[0])
	{
	    strcpy(nodename, comline->nodenames[0]);
	}
	else
	{
	    struct utsname utsname;

	    error = uname(&utsname);
	    if (error)
		die("uname failed!");

	    strcpy(nodename, utsname.nodename);
	}

	/* Look for the node in CCS, if we don't find it and uname has returned
	   a FQDN then strip the domain off and try again */
	memset(path, 0, MAX_PATH_LEN);
	sprintf(path, NODE_NAME_PATH, nodename);

	error = ccs_get(cd, path, &str);
	if (error)
	{
	    char *dot;
	    dot = strstr(nodename, ".");
	    if (dot)
	    {
		*dot = '\0';

		sprintf(path, NODE_NAME_PATH, nodename);
		error = ccs_get(cd, path, &str);
		if (error)
		    die("cannot find local node name \"%s\" in ccs", nodename);
	    }
	    else
	    {
		die("cannot find local node name \"%s\" in ccs", nodename);
	    }
	}
	free(str);

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
	}


	/* optional multicast name(s) with interfaces */

	comline->num_multicasts = 0;
	comline->num_interfaces = 0;

	for (i = 0; ; i++) {
		str = NULL;

		error = ccs_get(cd, MCAST_ADDR_PATH, &str);
		if (error || !str)
			break;

		/* If we get the same thing twice, it's probably the end of a
		   1-element list */

		if (i > 0 && strcmp(str, comline->multicast_names[i-1]) == 0) {
			free(str);
			break;
		}

		if (comline->verbose)
			printf("multicast address %s\n", str);

		comline->multicast_names[i] = str;
		comline->num_multicasts++;
	}

	for (i = 0; i < comline->num_multicasts; i++) {
		str = NULL;
		name = comline->multicast_names[i];
		memset(path, 0, MAX_PATH_LEN);
		sprintf(path, NODE_MCAST_IF_PATH, nodename, name);

		error = ccs_get(cd, path, &str);
		if (error || !str)
			die("no interface for multicast address %s", name);

		if (comline->verbose)
			printf("if %s for mcast address %s\n", str, name);

		comline->interfaces[i] = str;
		comline->num_interfaces++;
	}


	/* find our own number of votes */


	memset(path, 0, MAX_PATH_LEN);
	sprintf(path, NODE_VOTES_PATH, nodename);

	error = ccs_get(cd, path, &str);
	if (!error) {
		comline->votes = atoi(str);
		free(str);
	}


	/* get all alternative node names */

	comline->nodenames[0] = strdup(nodename);
	comline->num_nodenames = 1;

	memset(path, 0, MAX_PATH_LEN);
	sprintf(path, NODE_ALTNAMES_PATH, nodename);

	for (i = 1; ; i++) {
		str = NULL;

		error = ccs_get(cd, path, &str);
		if (error || !str)
			break;

		/* If we get the same thing twice, it's probably the end of a
		   1-element list */

		if (strcmp(str, comline->nodenames[i-1]) == 0) {
			free(str);
			break;
		}

		if (comline->verbose)
			printf("alternative node name %s\n", str);

		comline->nodenames[i] = str;
		comline->num_nodenames++;
	}


	/* two_node mode */

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
