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


#define CLUSTER_NAME_PATH	"/cluster/@name"
#define CONFIG_VERSION_PATH	"/cluster/@config_version"
#define EXP_VOTES_PATH		"/cluster/cman/@expected_votes"
#define TWO_NODE_PATH		"/cluster/cman/@two_node"
#define MCAST_ADDR_PATH		"/cluster/cman/multicast/@addr"
#define PORT_PATH		"/cluster/cman/@port"

#define NODEI_NAME_PATH		"/cluster/clusternodes/clusternode[%d]/@name"
#define NODE_NAME_PATH		"/cluster/clusternodes/clusternode[@name=\"%s\"]/@name"
#define NODE_VOTES_PATH		"/cluster/clusternodes/clusternode[@name=\"%s\"]/@votes"
#define NODE_NODEID_PATH	"/cluster/clusternodes/clusternode[@name=\"%s\"]/@nodeid"
#define NODE_IFNAME_PATH        "/cluster/clusternodes/clusternode[@name=\"%s\"]/@ifname"
#define NODE_ALTNAMES_PATH	"/cluster/clusternodes/clusternode[@name=\"%s\"]/altname/@name"
#define NODE_MCAST_IF_PATH	"/cluster/clusternodes/clusternode[@name=\"%s\"]/multicast[@addr=\"%s\"]/@interface"


/*
 * It's ok to let a node join the cluster even if it's not in cluster.conf.
 * It's similar to allowing the admin to lower expected_votes dynamically.  The
 * biggest risk with this kind of thing is that an inquorate cluster partition
 * will gain quorum creating a split-brain scenario where two partitions have
 * quorum.
 *
 * This poses no real danger to GFS.  Even in a split-brain cluster, the
 * fencing domain ensures that one half or the other is fenced before gfs usage
 * is allowed.  If both halves gain quorum, both halves will attempt to fence
 * each other instead of the normal scenario where the quorate half will fence
 * the inquorate half.  Regardless the outcome of this fencing battle, gfs
 * integrity is preserved.
 *
 * [This does require that the fence daemon only allow a node to join the fence
 * domain if that node's cluster nodename is in cluster.conf.  Mounting gfs is
 * then only permitted if the node is in the fence domain.]
 *
 * So, if the user wants a node that's not in cluster.conf to join the cluster
 * that's fine, they can always do that using the -X option to ignore
 * cluster.conf.  Here, when using ccs/cluster.conf, we'll assume that the user
 * has made an error if we find that a node's name is missing from cluster.conf
 * since that's the most likely situation and most helpful to the non-expert.
 */
 
int get_ccs_join_info(commandline_t *comline)
{
	char path[MAX_PATH_LEN];
	char nodename[MAX_NODE_NAME_LEN];
	char *str, *name, *cname = NULL, *dot = NULL;
	int cd, error, i, vote_sum = 0, node_count = 0;


	if (comline->config_version_opt ||
	    comline->votes_opt ||
	    comline->expected_votes_opt ||
	    comline->port_opt ||
	    comline->nodeid_opt)
		printf("command line options may override cluster.conf values\n");


	/* connect to ccsd */

	if (comline->clustername_opt)
		cname = comline->clustername;

	cd = ccs_force_connect(cname, 1);
	if (cd < 0)
		die("cannot connect to ccs (name=%s)",
		    cname ? comline->clustername : "none");


	/* cluster name */

	error = ccs_get(cd, CLUSTER_NAME_PATH, &str);
	if (error)
		die("cannot find cluster name in config file");

	if (comline->clustername_opt) {
		if (strcmp(cname, str))
			die("cluster names not equal %s %s", cname, str);
	} else
		strcpy(comline->clustername, str);
	free(str);


	/* our nodename */

	memset(nodename, 0, MAX_NODE_NAME_LEN);

	if (comline->num_nodenames > 0)
		strcpy(nodename, comline->nodenames[0]);
	else {
		struct utsname utsname;
		error = uname(&utsname);
		if (error)
			die("cannot get node name, uname failed");
		/* we set up the comline nodenames below */
		strcpy(nodename, utsname.nodename);
	}


	/* find our nodename in cluster.conf */

 retry_name:
	memset(path, 0, MAX_PATH_LEN);
	sprintf(path, NODE_NAME_PATH, nodename);

	error = ccs_get(cd, path, &str);
	if (error) {
		if (dot || !strstr(nodename, "."))
			die("local node name \"%s\" not found in cluster.conf",
			    nodename);
		dot = strstr(nodename, ".");
		*dot = '\0';
		goto retry_name;
	} else
		free(str);


	/* config version */

	if (!comline->config_version_opt) {
		error = ccs_get(cd, CONFIG_VERSION_PATH, &str);
		if (!error) {
			comline->config_version = atoi(str);
			free(str);
		}
	}


	/* sum node votes for expected */

	if (!comline->expected_votes_opt) {
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
	}


	/* optional port */

	if (!comline->port_opt) {
		error = ccs_get(cd, PORT_PATH, &str);
		if (!error) {
			comline->port = atoi(str);
			free(str);
		}
	}


	/* optional multicast name(s) with interfaces */

	if (!comline->num_multicasts) {
		comline->num_multicasts = 0;
		comline->num_interfaces = 0;

		for (i = 0; ; i++) {
			str = NULL;

			error = ccs_get(cd, MCAST_ADDR_PATH, &str);
			if (error || !str)
				break;

			/* If we get the same thing twice, it's probably the
			   end of a 1-element list */

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
	}



	/* find our own number of votes */

	if (!comline->votes_opt) {
		memset(path, 0, MAX_PATH_LEN);
		sprintf(path, NODE_VOTES_PATH, nodename);

		error = ccs_get(cd, path, &str);
		if (!error) {
			comline->votes = atoi(str);
			free(str);
		}
	}


	/* optional nodeid */

	if (!comline->nodeid_opt) {
		memset(path, 0, MAX_PATH_LEN);
		sprintf(path, NODE_NODEID_PATH, nodename);

		error = ccs_get(cd, path, &str);
		if (!error) {
			comline->nodeid = atoi(str);
			free(str);
		}
	}


	/* get all alternative node names */

	if (!comline->num_nodenames) {
		comline->nodenames[0] = strdup(nodename);
		comline->num_nodenames = 1;

		memset(path, 0, MAX_PATH_LEN);
		sprintf(path, NODE_ALTNAMES_PATH, nodename);

		for (i = 1; ; i++) {
			str = NULL;

			error = ccs_get(cd, path, &str);
			if (error || !str)
				break;

			/* If we get the same thing twice, it's probably the
			   end of a 1-element list */

			if (strcmp(str, comline->nodenames[i-1]) == 0) {
				free(str);
				break;
			}

			if (comline->verbose)
				printf("alternative node name %s\n", str);

			comline->nodenames[i] = str;
			comline->num_nodenames++;
		}
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
