/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005-2006 Red Hat, Inc.  All rights reserved.
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
#include <string.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <netdb.h>
#include <ifaddrs.h>

#include "list.h"
#include "cnxman-socket.h"
#include "cnxman-private.h"
#include "logging.h"
#include "commands.h"
#include "ais.h"
#include "ccs.h"


#define DEFAULT_PORT            5405

#define CONFIG_VERSION_PATH	"/cluster/@config_version"
#define CLUSTER_NAME_PATH	"/cluster/@name"

#define CLUSTER_ID_PATH 	"/cluster/cman/@cluster_id"
#define EXP_VOTES_PATH		"/cluster/cman/@expected_votes"
#define TWO_NODE_PATH		"/cluster/cman/@two_node"
#define MCAST_ADDR_PATH		"/cluster/cman/multicast/@addr"
#define PORT_PATH		"/cluster/cman/@port"
#define KEY_PATH		"/cluster/cman/@keyfile"

#define NODEI_NAME_PATH		"/cluster/clusternodes/clusternode[%d]/@name"
#define NODE_NAME_PATH_BYNAME	"/cluster/clusternodes/clusternode[@name=\"%s\"]/@name"
#define NODE_NAME_PATH_BYNUM	"/cluster/clusternodes/clusternode[%d]/@name"
#define NODE_VOTES_PATH		"/cluster/clusternodes/clusternode[@name=\"%s\"]/@votes"
#define NODE_NODEID_PATH	"/cluster/clusternodes/clusternode[@name=\"%s\"]/@nodeid"
#define NODE_ALTNAMES_PATH	"/cluster/clusternodes/clusternode[@name=\"%s\"]/altname[%d]/@name"
#define NODE_ALTNAMES_PORT	"/cluster/clusternodes/clusternode[@name=\"%s\"]/altname[%d]/@port"
#define NODE_ALTNAMES_MCAST	"/cluster/clusternodes/clusternode[@name=\"%s\"]/altname[%d]/@mcast"

#define MAX_NODENAMES 10
#define MAX_PATH_LEN PATH_MAX

/* Local vars - things we get from ccs */
static int nodeid;
static char *nodenames[MAX_NODENAMES];
static int  portnums[MAX_NODENAMES];
static char *mcast[MAX_NODENAMES];
static int num_nodenames;
static int two_node;
       char *key_filename;
static char *mcast_name;
static unsigned char votes;
static unsigned int expected_votes;
static unsigned short cluster_id;
static char cluster_name[MAX_CLUSTER_NAME_LEN + 1];

static uint16_t generate_cluster_id(char *name)
{
	int i;
	int value = 0;

	for (i=0; i<strlen(name); i++) {
		value <<= 1;
		value += name[i];
	}
	P_MEMB("Generated cluster id for '%s' is %d\n", name, value & 0xFFFF);
	return value & 0xFFFF;
}


/* Get all the cluster node names from CCS and
 * add them to our node list.
 * Called when we start up and on "cman_tool version".
 */
int read_ccs_nodes(unsigned int *config_version, int check_nodeids)
{
    int ctree;
    char *nodename;
    char *str;
    int error;
    int i;
    int expected = 0;
    unsigned int config;

    /* Open the config file */
    ctree = ccs_force_connect(NULL, 1);
    if (ctree < 0) {
	    log_msg(LOG_ERR, "Error connecting to CCS");
	    write_cman_pipe("Cannot connect to CCS");
	    return -1;
    }

    /* New config version */
    if (!ccs_get(ctree, CONFIG_VERSION_PATH, &str)) {
	    config = atoi(str);
	    free(str);
	    *config_version = config;
    }

    /* This overrides any other expected votes calculation /except/ for
       one specified on a join command-line */
    if (!ccs_get(ctree, EXP_VOTES_PATH, &str)) {
	    expected = atoi(str);
	    free(str);
    }

    if (!ccs_get(ctree, TWO_NODE_PATH, &str)) {
	    two_node = atoi(str);
	    free(str);
    }

    for (i=1;;i++) {
	char nodekey[256];
	char key[256];
	int  votes=0, nodeid=0;

	sprintf(nodekey, NODE_NAME_PATH_BYNUM, i);
	error = ccs_get(ctree, nodekey, &nodename);
	if (error)
	    break;

	sprintf(key, NODE_VOTES_PATH, nodename);
	if (!ccs_get(ctree, key, &str)) {
	    votes = atoi(str);
	    free(str);
	} else
	    votes = 1;

	sprintf(key, NODE_NODEID_PATH, nodename);
	if (!ccs_get(ctree, key, &str))	{
	    nodeid = atoi(str);
	    free(str);

	}

	if (check_nodeids && nodeid == 0) {
		char message[132];

		sprintf(message, "No node ID for %s, run 'ccs_tool addnodeids' to fix", nodename);
		log_msg(LOG_ERR, message);
		write_cman_pipe(message);
		return -1;
	}

	P_MEMB("Got node %s from ccs (id=%d, votes=%d)\n", nodename, nodeid, votes);
	add_ccs_node(nodename, nodeid, votes, expected);

	free(nodename);
    }

    if (expected)
	    override_expected(expected);

    /* Finished with config file */
    ccs_disconnect(ctree);

    return 0;
}

static char *default_mcast(uint16_t cluster_id)
{
        struct addrinfo *ainfo;
        struct addrinfo ahints;
	int ret;
	int family;
	static char addr[132];

        memset(&ahints, 0, sizeof(ahints));

        /* Lookup the the nodename address and use it's IP type to
	   default a multicast address */
        ret = getaddrinfo(nodenames[0], NULL, &ahints, &ainfo);
	if (ret) {
		log_msg(LOG_ERR, "Can't determine address family of nodename %s\n", nodenames[0]);
		write_cman_pipe("Can't determine address family of nodename");
		return NULL;
	}

	family = ainfo->ai_family;
	freeaddrinfo(ainfo);

	if (family == AF_INET) {
		snprintf(addr, sizeof(addr), "239.192.%d.%d", cluster_id >> 8, cluster_id % 0xFF);
		return addr;
	}
	if (family == AF_INET6) {
		snprintf(addr, sizeof(addr), "ff15::%x", cluster_id);
		return addr;
	}

	return NULL;
}

static int join(void)
{
	int error, i;

	error = cman_set_nodename(nodenames[0]);
	error = cman_set_nodeid(nodeid);

        /*
	 * Setup join information
	 */
	error = cman_join_cluster(cluster_name, cluster_id,
				  two_node, expected_votes);
	if (error == -EINVAL) {
		write_cman_pipe("Cannot start, cluster name is too long or other CCS error");
		return error;
	}
	if (error) {
		write_cman_pipe("Cannot start, ais may already be running");
		return error;
	}

	/*
	 * Setup the interface/multicast addresses
	 */
	for (i = 0; i<num_nodenames; i++) {
		error = ais_add_ifaddr(mcast[i], nodenames[i], portnums[i]);
		if (error) {
			if (errno == EADDRINUSE)
				write_cman_pipe("Local host name resolves to 127.0.0.1; fix /etc/hosts before starting cluster.");
			else
				write_cman_pipe("Multicast and node address families differ.");
			return error;
		}
	}

	return 0;
}

static int verify_nodename(int cd, char *nodename)
{
	char path[MAX_PATH_LEN];
	char nodename2[MAX_CLUSTER_MEMBER_NAME_LEN+1];
	char nodename3[MAX_CLUSTER_MEMBER_NAME_LEN+1];
	char *str, *dot = NULL;
	struct ifaddrs *ifa, *ifa_list;
	struct sockaddr *sa;
	int error, i;

	/* nodename is either from commandline or from uname */
	str = NULL;
	memset(path, 0, MAX_PATH_LEN);
	sprintf(path, NODE_NAME_PATH_BYNAME, nodename);

	error = ccs_get(cd, path, &str);
	if (!error) {
		free(str);
		return 0;
	}

	/* If nodename was from uname, try a domain-less version of it */
	strcpy(nodename2, nodename);
	dot = strstr(nodename2, ".");
	if (dot) {
		*dot = '\0';

		str = NULL;
		memset(path, 0, MAX_PATH_LEN);
		sprintf(path, NODE_NAME_PATH_BYNAME, nodename2);

		error = ccs_get(cd, path, &str);
		if (!error) {
			free(str);
			strcpy(nodename, nodename2);
			return 0;
		}
	}


	/* If nodename (from uname) is domain-less, try to match against
	   cluster.conf names which may have domainname specified */
	for (i = 1; ; i++) {
		int len;
		str = NULL;
		memset(path, 0, 256);
		sprintf(path, "/cluster/clusternodes/clusternode[%d]/@name", i);

		error = ccs_get(cd, path, &str);
		if (error || !str)
			break;

		strcpy(nodename3, str);
		dot = strstr(nodename3, ".");
		if (dot)
			len = dot-nodename3;
		else
			len = strlen(nodename3);

		if (strlen(nodename2) == len &&
		    !strncmp(nodename2, nodename3, len)) {
			free(str);
			strcpy(nodename, nodename3);
			return 0;
		}

		free(str);
	}

	/* The cluster.conf names may not be related to uname at all,
	   they may match a hostname on some network interface.
	   NOTE: This is IPv4 only */
	error = getifaddrs(&ifa_list);
	if (error)
		return -1;

	for (ifa = ifa_list; ifa; ifa = ifa->ifa_next) {

		/* Restore this */
		strcpy(nodename2, nodename);
		sa = ifa->ifa_addr;
		if (!sa || sa->sa_family != AF_INET)
			continue;

		error = getnameinfo(sa, sizeof(*sa), nodename2,
				    sizeof(nodename2), NULL, 0, 0);
		if (error)
			goto out;

		str = NULL;
		memset(path, 0, 256);
		sprintf(path, NODE_NAME_PATH_BYNAME, nodename2);

		error = ccs_get(cd, path, &str);
		if (!error) {
			free(str);
			strcpy(nodename, nodename2);
			goto out;
		}

		/* truncate this name and try again */

		dot = strstr(nodename2, ".");
		if (!dot)
			continue;
		*dot = '\0';

		str = NULL;
		memset(path, 0, 256);
		sprintf(path, NODE_NAME_PATH_BYNAME, nodename2);

		error = ccs_get(cd, path, &str);
		if (!error) {
			free(str);
			strcpy(nodename, nodename2);
			goto out;
		}

		/* See if it's the IP address that's in cluster.conf */
		error = getnameinfo(sa, sizeof(*sa), nodename2,
				    sizeof(nodename2), NULL, 0, NI_NUMERICHOST);
		if (error)
			goto out;

		str = NULL;
		memset(path, 0, 256);
		sprintf(path, NODE_NAME_PATH_BYNAME, nodename2);

		error = ccs_get(cd, path, &str);
		if (!error) {
			free(str);
			strcpy(nodename, nodename2);
			goto out;
		}
	}

	error = -1;
 out:
	freeifaddrs(ifa_list);
	return error;
}


static int get_ccs_join_info(void)
{
	char path[MAX_PATH_LEN];
	char nodename[MAX_CLUSTER_MEMBER_NAME_LEN+1];
	char *str, *name, *cname = NULL;
	int cd, error, i, vote_sum = 0, node_count = 0;
	unsigned short port = 0;

	/* Connect to ccsd */
	if (getenv("CMAN_CLUSTER_NAME")) {
		cname = getenv("CMAN_CLUSTER_NAME");
		log_msg(LOG_INFO, "Using override cluster name %s\n", cname);
	}

	cd = ccs_force_connect(cname, 1);
	if (cd < 0) {
		log_msg(LOG_ERR, "Error connecting to CCS");
		write_cman_pipe("Can't connect to CCSD");
		return -ENOTCONN;
	}

	/* Cluster name */
	error = ccs_get(cd, CLUSTER_NAME_PATH, &str);
	if (error) {
		log_msg(LOG_ERR, "cannot find cluster name in config file");
		write_cman_pipe("Can't find cluster name in CCS");
		return -ENOENT;
	}

	if (cname) {
		if (strcmp(cname, str)) {
			log_msg(LOG_ERR, "cluster names not equal %s %s", cname, str);
			write_cman_pipe("Cluster name in CCS does not match that passed to cman_tool");
			return -ENOENT;
		}
	} else {
		strcpy(cluster_name, str);
	}
	free(str);

	error = ccs_get(cd, CLUSTER_ID_PATH, &str);
	if (!error) {
		cluster_id = atoi(str);
		free(str);
	}
	else {
		cluster_id = generate_cluster_id(cluster_name);
	}

	/* our nodename */
	memset(nodename, 0, sizeof(nodename));

	if (getenv("CMAN_NODENAME")) {
		strcpy(nodename, getenv("CMAN_NODENAME"));
		log_msg(LOG_INFO, "Using override node name %s\n", nodename);

		sprintf(path, NODE_NAME_PATH_BYNAME, nodename);

		error = ccs_get(cd, path, &str);
		if (!error) {
			free(str);
		}
		else {
			log_msg(LOG_ERR, "Overridden node name %s is not in CCS", nodename);
			write_cman_pipe("Overridden node name is not in CCS");
			return -ENOENT;
		}
	}
	else {
		struct utsname utsname;
		error = uname(&utsname);
		if (error) {
			log_msg(LOG_ERR, "cannot get node name, uname failed");
			write_cman_pipe("Can't determine local node name");
			return -ENOENT;
		}
		strcpy(nodename, utsname.nodename);
	}


	/* Find our nodename in cluster.conf */
	error = verify_nodename(cd, nodename);
	if (error) {
		log_msg(LOG_ERR, "local node name \"%s\" not found in cluster.conf",
			nodename);
		write_cman_pipe("Can't find local node name in cluster.conf");
		return -ENOENT;
	}
	nodenames[0] = strdup(nodename);

	expected_votes = 0;
	if (getenv("CMAN_EXPECTEDVOTES")) {
		expected_votes = atoi(getenv("CMAN_EXPECTEDVOTES"));
		if (expected_votes < 1) {
			log_msg(LOG_ERR, "CMAN_EXPECTEDVOTES environment variable is invalid, ignoring");
			expected_votes = 0;
		}
		else {
			log_msg(LOG_INFO, "Using override expected votes %d\n", expected_votes);
		}
	}

	/* Sum node votes for expected */
	if (expected_votes == 0) {
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
				if (atoi(str) < 0) {
					log_msg(LOG_ERR, "negative votes not allowed");
					write_cman_pipe("Found negative votes for this node in CCS");
					return -EINVAL;
				}
				vote_sum += atoi(str);
				free(str);
			}
		}

		/* optional expected_votes supercedes vote sum */

		error = ccs_get(cd, EXP_VOTES_PATH, &str);
		if (!error) {
			expected_votes = atoi(str);
			free(str);
		} else
			expected_votes = vote_sum;
	}

	/* optional port */
	if (getenv("CMAN_IP_PORT")) {
		port = atoi(getenv("CMAN_IP_PORT"));
		log_msg(LOG_INFO, "Using override IP port %d\n", port);
	}

	if (!port) {
		error = ccs_get(cd, PORT_PATH, &str);
		if (!error) {
			port = atoi(str);
			free(str);
		}
		else
			port = DEFAULT_PORT;
	}
	portnums[0] = port;

	/* optional security key filename */
	if (getenv("CMAN_KEYFILE")) {
		key_filename = strdup(getenv("CMAN_KEYFILE"));
	}
	else {
		error = ccs_get(cd, KEY_PATH, &str);
		if (!error) {
			key_filename = str;
		}
	}

	/* find our own number of votes */
	if (getenv("CMAN_VOTES")) {
		votes = atoi(getenv("CMAN_VOTES"));
		log_msg(LOG_INFO, "Using override votes %d\n", votes);
	}

	if (!votes) {
		memset(path, 0, MAX_PATH_LEN);
		sprintf(path, NODE_VOTES_PATH, nodename);

		error = ccs_get(cd, path, &str);
		if (!error) {
			int votestmp = atoi(str);
			if (votestmp < 0 || votestmp > 255) {
				log_msg(LOG_ERR, "invalid votes value %d", votes);
				write_cman_pipe("Found invalid votes for node in CCS");
				return -EINVAL;
			}
			votes = votestmp;
			free(str);
		}
		else {
			votes = 1;
		}
	}


	/* nodeid */
	if (getenv("CMAN_NODEID")) {
		nodeid = atoi(getenv("CMAN_NODEID"));
		log_msg(LOG_INFO, "Using override nodeid %d\n", nodeid);
	}

	if (!nodeid) {
		memset(path, 0, MAX_PATH_LEN);
		sprintf(path, NODE_NODEID_PATH, nodename);

		error = ccs_get(cd, path, &str);
		if (!error) {
			nodeid = atoi(str);
			free(str);
		}
	}

	if (!nodeid) {
		log_msg(LOG_ERR, "No nodeid specified in cluster.conf");
		write_cman_pipe("CCS does not have a nodeid for this node, run 'ccs_tool addnodeids' to fix");
		return -EINVAL;
	}

	if (getenv("CMAN_MCAST_ADDR")) {
		mcast_name = getenv("CMAN_MCAST_ADDR");
		log_msg(LOG_INFO, "Using override multicast address %s\n", mcast_name);
	}

	/* Optional multicast name */
	if (!mcast_name) {
		error = ccs_get(cd, MCAST_ADDR_PATH, &str);
		if (!error) {
			mcast_name = str;
		}
	}

	if (!mcast_name) {
		mcast_name = default_mcast(cluster_id);
		log_msg(LOG_INFO, "Using default multicast address of %s\n", mcast_name);
	}

	mcast[0] = mcast_name;

	/* Get all alternative node names */
	num_nodenames = 1;

	memset(path, 0, MAX_PATH_LEN);


	for (i = 1; ; i++) {
		str = NULL;
		sprintf(path, NODE_ALTNAMES_PATH, nodename, i);

		error = ccs_get(cd, path, &str);
		if (error || !str)
			break;

		nodenames[i] = str;

		sprintf(path, NODE_ALTNAMES_PORT, nodename, i);
		error = ccs_get(cd, path, &str);
		if (error || !str) {
			portnums[i] = portnums[0];
		}
		else {
			portnums[i] = atoi(str);
			free(str);
		}

		sprintf(path, NODE_ALTNAMES_MCAST, nodename, i);
		error = ccs_get(cd, path, &str);
		if (error || !str) {
			mcast[i] = mcast_name;
		}
		else {
			mcast[i] = str;
		}

		num_nodenames++;
	}


	/* two_node mode */
	error = ccs_get(cd, TWO_NODE_PATH, &str);
	if (!error) {
		two_node = atoi(str);
		free(str);
		if (two_node) {
			if (node_count != 2 || vote_sum != 2) {
				log_msg(LOG_ERR, "the two-node option requires exactly two "
					"nodes with one vote each and expected "
					"votes of 1 (node_count=%d vote_sum=%d)",
					node_count, vote_sum);
				write_cman_pipe("two_node set but there are more than 2 nodes");
				return -EINVAL;
			}

			if (votes != 1) {
				log_msg(LOG_ERR, "the two-node option requires exactly two "
					"nodes with one vote each and expected "
					"votes of 1 (votes=%d)", votes);
				write_cman_pipe("two_node set but votes not set to 1");
				return -EINVAL;
			}
		}
	}

	ccs_disconnect(cd);
	return 0;
}



/* Read just the stuff we need to get started.
   This does what 'cman_tool join' used to to */
int read_ccs_config()
{
	int error;

	error = get_ccs_join_info();
	if (error) {
		log_msg(LOG_ERR, "Error reading CCS info, cannot start");
		return error;
	}

	error = join();

	return error;
}
