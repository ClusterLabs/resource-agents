/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005-2008 Red Hat, Inc.  All rights reserved.
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
#include <errno.h>
#include <netdb.h>

#include <openais/service/objdb.h>
#include <openais/service/swab.h>
#include <openais/service/logsys.h>

#include "list.h"
#include "cnxman-socket.h"
#include "cnxman-private.h"
#include "logging.h"
#include "commands.h"
#include "cmanconfig.h"
LOGSYS_DECLARE_SUBSYS (CMAN_NAME, LOG_INFO);
#include "nodelist.h"
#include "ais.h"
#include "cman.h"

/* Local vars - things we get from ccs */
       int two_node;
static int nodeid;
static unsigned int cluster_id;
static char cluster_name[MAX_CLUSTER_NAME_LEN + 1];
static unsigned int expected_votes;
static char *our_nodename;
static int our_votes;
static unsigned int cluster_parent_handle;

/* Get all the cluster node names from objdb and
 * add them to our node list.
 * Called when we start up and on "cman_tool version".
 */
int read_cman_nodes(struct objdb_iface_ver0 *objdb, unsigned int *config_version, int check_nodeids)
{
    int error;
    unsigned int expected = 0;
    unsigned int votes = 0;
    int nodeid;
    unsigned int object_handle;
    unsigned int nodes_handle;
    unsigned int parent_handle;
    char *nodename;

    /* New config version */
    objdb_get_int(objdb, cluster_parent_handle, "config_version", config_version);

    objdb->object_find_reset(cluster_parent_handle);

    if (objdb->object_find(cluster_parent_handle,
			   "cman", strlen("cman"),
			   &object_handle) == 0)
    {
	    /* This overrides any other expected votes calculation /except/ for
	       one specified on a join command-line */
	    objdb_get_int(objdb, object_handle, "expected_votes", &expected);
	    objdb_get_int(objdb, object_handle, "two_node", (unsigned int *)&two_node);
	    objdb_get_int(objdb, object_handle, "cluster_id", &cluster_id);
	    objdb_get_string(objdb, object_handle, "nodename", &our_nodename);
	    objdb_get_int(objdb, object_handle, "max_queued", &max_outstanding_messages);
    }

    clear_reread_flags();

    /* Get the nodes list */
    nodes_handle = nodeslist_init(objdb, cluster_parent_handle, &parent_handle);
    do {
	    if (objdb_get_string(objdb, nodes_handle, "name", &nodename)) {
		    nodes_handle = nodeslist_next(objdb, parent_handle);
		    continue;
	    }

	    objdb_get_int(objdb, nodes_handle, "votes", (unsigned int *)&votes);
	    if (votes == 0)
		    votes = 1;

	    objdb_get_int(objdb, nodes_handle, "nodeid", (unsigned int *)&nodeid);

	    if (check_nodeids && nodeid == 0) {
		    char message[132];

		    snprintf(message, sizeof(message),
			     "No node ID for %s, run 'ccs_tool addnodeids' to fix",
			     nodename);
		    log_printf(LOG_ERR, "%s", message);
		    write_cman_pipe(message);
		    error = -EINVAL;
		    goto out_err;
	    }

	    P_MEMB("Got node %s from ccs (id=%d, votes=%d)\n", nodename, nodeid, votes);
	    add_ccs_node(nodename, nodeid, votes, expected);
	    nodes_handle = nodeslist_next(objdb, parent_handle);
    } while (nodes_handle);

    if (expected)
	    override_expected(expected);

    remove_unread_nodes();
    error = 0;

out_err:
    return error;
}

static int join(struct objdb_iface_ver0 *objdb)
{
	int error;
	error = cman_set_nodename(our_nodename);
	error = cman_set_nodeid(nodeid);

        /*
	 * Setup join information
	 */
	error = cman_join_cluster(objdb, cluster_name, cluster_id,
				  two_node, our_votes, expected_votes);
	if (error == -EINVAL) {
		write_cman_pipe("Cannot start, cluster name is too long or other CCS error");
		return error;
	}
	if (error) {
		write_cman_pipe("Cannot start, ais may already be running");
		return error;
	}

	return 0;
}

static int get_cman_join_info(struct objdb_iface_ver0 *objdb)
{
	char *cname = NULL;
	int  error, vote_sum = 0, node_count = 0;
	int votes=0;
	unsigned int object_handle;
	unsigned int node_object;

	/* Cluster name */
	if (objdb_get_string(objdb, cluster_parent_handle, "name", &cname)) {
		log_printf(LOG_ERR, "cannot find cluster name in config file");
		write_cman_pipe("Can't find cluster name in CCS");
		error = -ENOENT;
		goto out;
	}

	strcpy(cluster_name, cname);

	expected_votes = 0;
	if (getenv("CMAN_EXPECTEDVOTES")) {
		expected_votes = atoi(getenv("CMAN_EXPECTEDVOTES"));
		if (expected_votes < 1) {
			log_printf(LOG_ERR, "CMAN_EXPECTEDVOTES environment variable is invalid, ignoring");
			expected_votes = 0;
		}
		else {
			log_printf(LOG_INFO, "Using override expected votes %d\n", expected_votes);
		}
	}

	/* Sum node votes for expected */
	if (expected_votes == 0) {
		unsigned int nodes_handle;
		unsigned int parent_handle;

		nodes_handle = nodeslist_init(objdb, cluster_parent_handle, &parent_handle);
		do {
			int votes;

			node_count++;

			objdb_get_int(objdb, nodes_handle, "votes", (unsigned int *)&votes);
			if (votes == 0)
				votes = 1;

			if (votes < 0) {
				log_printf(LOG_ERR, "negative votes not allowed");
				write_cman_pipe("Found negative votes for this node in CCS");
				error = -EINVAL;
				goto out;
			}
			vote_sum += votes;
			nodes_handle = nodeslist_next(objdb, parent_handle);
		} while (nodes_handle);

		objdb->object_find_reset(cluster_parent_handle);
		if (objdb->object_find(cluster_parent_handle,
				       "cman", strlen("cman"),
				       &object_handle) == 0)
		{

			/* optional expected_votes supercedes vote sum */
			objdb_get_int(objdb, object_handle, "expected_votes", (unsigned int *)&expected_votes);
			if (!expected_votes)
				expected_votes = vote_sum;
		}
	}

	/* find our own number of votes */
	if (getenv("CMAN_VOTES")) {
		votes = atoi(getenv("CMAN_VOTES"));
		log_printf(LOG_INFO, "Using override votes %d\n", votes);
	}

	node_object = nodelist_byname(objdb, cluster_parent_handle, our_nodename);
	if (!node_object) {
		log_printf(LOG_ERR, "unable to find votes for %s", our_nodename);
		write_cman_pipe("Unable to find votes for node in CCS");
		return -E2BIG;
	}

	if (!votes) {
		unsigned int votestmp=-1;
		objdb_get_int(objdb, node_object, "votes", &votestmp);
		if (votestmp == -1)
			votestmp = 1;

		if (votestmp < 0 || votestmp > 255) {
			log_printf(LOG_ERR, "invalid votes value %d", votestmp);
			write_cman_pipe("Found invalid votes for node in CCS");
			return -EINVAL;
		}
		votes = votestmp;
	}
	if (!votes) {
		votes = 1;
	}
	our_votes = votes;

	/* nodeid */
	if (getenv("CMAN_NODEID")) {
		nodeid = atoi(getenv("CMAN_NODEID"));
		log_printf(LOG_INFO, "Using override nodeid %d\n", nodeid);
	}

	if (!nodeid) {
		objdb_get_int(objdb, node_object, "nodeid", (unsigned int *)&nodeid);
	}

	if (!nodeid) {
		log_printf(LOG_ERR, "No nodeid specified in cluster.conf");
		write_cman_pipe("CCS does not have a nodeid for this node, run 'ccs_tool addnodeids' to fix");
		return -EINVAL;
	}

	/* two_node mode */
	if (two_node) {
		if (node_count != 2 || vote_sum != 2) {
			log_printf(LOG_ERR, "the two-node option requires exactly two "
				   "nodes with one vote each and expected "
				   "votes of 1 (node_count=%d vote_sum=%d)",
				   node_count, vote_sum);
			write_cman_pipe("two_node set but there are more than 2 nodes");
			error = -EINVAL;
			goto out;
		}

		if (votes != 1) {
			log_printf(LOG_ERR, "the two-node option requires exactly two "
				   "nodes with one vote each and expected "
				   "votes of 1 (votes=%d)", votes);
			write_cman_pipe("two_node set but votes not set to 1");
			error = -EINVAL;
			goto out;
		}
	}

	error = 0;

out:
	return error;
}



/* Read the stuff we need to get started.
   This does what 'cman_tool join' used to to */
int read_cman_config(struct objdb_iface_ver0 *objdb, unsigned int *config_version)
{
	int error;

	objdb->object_find_reset(OBJECT_PARENT_HANDLE);
	objdb->object_find(OBJECT_PARENT_HANDLE,
			   "cluster", strlen("cluster"), &cluster_parent_handle);

	read_cman_nodes(objdb, config_version, 1);
	error = get_cman_join_info(objdb);
	if (error) {
		log_printf(LOG_ERR, "Error reading configuration, cannot start");
		return error;
	}

	error = join(objdb);

	return error;
}
