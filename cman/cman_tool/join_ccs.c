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
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ifaddrs.h>

#include "ccs.h"
#include "libcman.h"
#include "cman_tool.h"


#define CLUSTER_NAME_PATH	"/cluster/@name"
#define CONFIG_VERSION_PATH	"/cluster/@config_version"
#define EXP_VOTES_PATH		"/cluster/cman/@expected_votes"
#define TWO_NODE_PATH		"/cluster/cman/@two_node"
#define MCAST_ADDR_PATH		"/cluster/cman/multicast/@addr"
#define PORT_PATH		"/cluster/cman/@port"
#define KEY_PATH		"/cluster/cman/@keyfile"

#define NODEI_NAME_PATH		"/cluster/clusternodes/clusternode[%d]/@name"
#define NODE_NAME_PATH		"/cluster/clusternodes/clusternode[@name=\"%s\"]/@name"
#define NODE_VOTES_PATH		"/cluster/clusternodes/clusternode[@name=\"%s\"]/@votes"
#define NODE_NODEID_PATH	"/cluster/clusternodes/clusternode[@name=\"%s\"]/@nodeid"
#define NODE_IFNAME_PATH        "/cluster/clusternodes/clusternode[@name=\"%s\"]/@ifname"
#define NODE_ALTNAMES_PATH	"/cluster/clusternodes/clusternode[@name=\"%s\"]/altname/@name"

int verify_nodename(commandline_t *comline, int cd, char *nodename)
{
	char path[MAX_PATH_LEN];
	char nodename2[MAX_NODE_NAME_LEN];
	char nodename3[MAX_NODE_NAME_LEN];
	char *str, *dot = NULL;
	struct ifaddrs *ifa, *ifa_list;
	struct sockaddr *sa;
	int error, i;


	/* nodename is either from commandline or from uname */

	str = NULL;
	memset(path, 0, MAX_PATH_LEN);
	sprintf(path, NODE_NAME_PATH, nodename);

	error = ccs_get(cd, path, &str);
	if (!error) {
		free(str);
		return 0;
	}

	if (comline->verbose)
		printf("nodename %s not found\n", nodename);


	/* if nodename was on command line, don't try variations */

	if (comline->num_nodenames > 0)
		return -1;


	/* if nodename was from uname, try a domain-less version of it */

	strcpy(nodename2, nodename);
	dot = strstr(nodename2, ".");
	if (dot) {
		*dot = '\0';

		str = NULL;
		memset(path, 0, MAX_PATH_LEN);
		sprintf(path, NODE_NAME_PATH, nodename2);

		error = ccs_get(cd, path, &str);
		if (!error) {
			free(str);
			strcpy(nodename, nodename2);
			return 0;
		}

		if (comline->verbose)
			printf("nodename %s (truncated) not found\n", nodename2);
	}


	/* if nodename (from uname) is domain-less, try to match against
	   cluster.conf names which may have domainname specified */

	for (i = 1; ; i++) {
		str = NULL;
		memset(path, 0, 256);
		sprintf(path, "/cluster/clusternodes/clusternode[%d]/@name", i);

		error = ccs_get(cd, path, &str);
		if (error || !str)
			break;

		strcpy(nodename3, str);
		dot = strstr(nodename3, ".");
		if (dot)
			*dot = '\0';

		if (strlen(nodename2) == strlen(nodename3) &&
		    !strncmp(nodename2, nodename3, strlen(nodename3))) {
			free(str);
			strcpy(nodename, nodename3);
			return 0;
		}

		if (comline->verbose)
			printf("nodename %s doesn't match %s (%s in cluster.conf)\n",
				nodename2, nodename3, str);
		free(str);
	}


	/* the cluster.conf names may not be related to uname at all,
	   they may match a hostname on some network interface */

	error = getifaddrs(&ifa_list);
	if (error)
		return -1;

	for (ifa = ifa_list; ifa; ifa = ifa->ifa_next) {
		sa = ifa->ifa_addr;
		if (!sa || sa->sa_family != AF_INET)
			continue;

		error = getnameinfo(sa, sizeof(*sa), nodename2,
				    sizeof(nodename2), NULL, 0, 0);
		if (error)
			goto out;

		str = NULL;
		memset(path, 0, 256);
		sprintf(path, NODE_NAME_PATH, nodename2);

		error = ccs_get(cd, path, &str);
		if (!error) {
			free(str);
			strcpy(nodename, nodename2);
			goto out;
		}

		if (comline->verbose)
			printf("nodename %s (if %s) not found\n", nodename2,
			       ifa->ifa_name);

		/* truncate this name and try again */

		dot = strstr(nodename2, ".");
		if (!dot)
			continue;
		*dot = '\0';

		str = NULL;
		memset(path, 0, 256);
		sprintf(path, NODE_NAME_PATH, nodename2);

		error = ccs_get(cd, path, &str);
		if (!error) {
			free(str);
			strcpy(nodename, nodename2);
			goto out;
		}

		if (comline->verbose)
			printf("nodename %s (if %s truncated) not found\n",
				nodename2, ifa->ifa_name);
	}

	error = -1;
 out:
	freeifaddrs(ifa_list);
	return error;
}



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
	char *str, *name, *cname = NULL;
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

	error = verify_nodename(comline, cd, nodename);
	if (error)
		die("local node name \"%s\" not found in cluster.conf",
		    nodename);
	if (comline->verbose)
		printf("selected nodename %s\n", nodename);


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
				if (atoi(str) < 0)
					die("negative votes not allowed");
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

	/* optional security key filename */

	if (!comline->key_filename) {
		error = ccs_get(cd, KEY_PATH, &str);
		if (!error) {
			comline->key_filename = str;
		}
	}


	/* optional multicast name */
	str = NULL;
	error = ccs_get(cd, MCAST_ADDR_PATH, &str);
	if (str) {
		if (comline->verbose)
			printf("multicast address %s\n", str);

		comline->multicast_addr = str;
	}

	/* find our own number of votes */

	if (!comline->votes_opt) {
		memset(path, 0, MAX_PATH_LEN);
		sprintf(path, NODE_VOTES_PATH, nodename);

		error = ccs_get(cd, path, &str);
		if (!error) {
			comline->votes = atoi(str);
			if (comline->votes < 0 || comline->votes > 255)
				die("invalid votes value %d", comline->votes);
			free(str);
		}
		else {
			comline->votes = 1;
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
			if (node_count != 2 || vote_sum != 2)
				die("the two-node option requires exactly two "
				    "nodes with one vote each and expected "
				    "votes of 1 (node_count=%d vote_sum=%d)",
				    node_count, vote_sum);

			if (comline->votes_opt && comline->votes != 1)
				die("the two-node option requires exactly two "
				    "nodes with one vote each and expected "
                                    "votes of 1 (votes=%d)", comline->votes);

			if (!comline->votes_opt && comline->votes != 0 &&
			    comline->votes != 1)
				die("the two-node option requires exactly two "
				     "nodes with one vote each and expected "
				     "votes of 1 (votes=%d)", comline->votes);

			/* if no comline votes option and no votes value found
			   in cluster.conf, then votes is set to DEFAULT_VOTES
			   (1) in check_arguements() */
		}
	}

	ccs_disconnect(cd);
	return 0;
}
