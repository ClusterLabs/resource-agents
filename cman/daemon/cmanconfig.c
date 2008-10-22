#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <netinet/in.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>

#include <corosync/ipc_gen.h>
#include <corosync/engine/coroapi.h>
#include <corosync/engine/logsys.h>

#include "list.h"
#include "cnxman-socket.h"
#include "cnxman-private.h"
#include "logging.h"
#include "commands.h"
#include "cman.h"
#define OBJDB_API struct corosync_api_v1
#include "cmanconfig.h"
#include "nodelist.h"
#include "ais.h"

LOGSYS_DECLARE_SUBSYS (CMAN_NAME, LOG_INFO);

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
int read_cman_nodes(struct corosync_api_v1 *corosync, unsigned int *config_version, int check_nodeids)
{
    int error;
    unsigned int expected = 0;
    unsigned int votes = 0;
    int nodeid;
    unsigned int object_handle;
    unsigned int nodes_handle;
    unsigned int find_handle;
    char *nodename;

    /* New config version */
    objdb_get_int(corosync, cluster_parent_handle, "config_version", config_version,0);

    corosync->object_find_create(cluster_parent_handle, "cman", strlen("cman"), &find_handle);

    if (corosync->object_find_next(find_handle, &object_handle) == 0)
    {
	    /* This overrides any other expected votes calculation /except/ for
	       one specified on a join command-line */
	    objdb_get_int(corosync, object_handle, "expected_votes", &expected, 0);
	    objdb_get_int(corosync, object_handle, "two_node", (unsigned int *)&two_node, 0);
	    objdb_get_int(corosync, object_handle, "cluster_id", &cluster_id, 0);
	    objdb_get_string(corosync, object_handle, "nodename", &our_nodename);
	    objdb_get_int(corosync, object_handle, "max_queued", &max_outstanding_messages, DEFAULT_MAX_QUEUED);
    }
    corosync->object_find_destroy(find_handle);

    clear_reread_flags();

    /* Get the nodes list */
    nodes_handle = nodeslist_init(corosync, cluster_parent_handle, &find_handle);
    do {
	    if (objdb_get_string(corosync, nodes_handle, "name", &nodename)) {
		    nodes_handle = nodeslist_next(corosync, find_handle);
		    continue;
	    }

	    objdb_get_int(corosync, nodes_handle, "votes", (unsigned int *)&votes, 1);
	    objdb_get_int(corosync, nodes_handle, "nodeid", (unsigned int *)&nodeid, 0);
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
	    nodes_handle = nodeslist_next(corosync, find_handle);
    } while (nodes_handle);
    corosync->object_find_destroy(find_handle);

    if (expected)
	    override_expected(expected);

    remove_unread_nodes();
    error = 0;

out_err:
    return error;
}

static int join(struct corosync_api_v1 *corosync)
{
	int error;
	error = cman_set_nodename(our_nodename);
	error = cman_set_nodeid(nodeid);

        /*
	 * Setup join information
	 */
	error = cman_join_cluster(corosync, cluster_name, cluster_id,
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

static int get_cman_join_info(struct corosync_api_v1 *corosync)
{
	char *cname = NULL;
	int  error, vote_sum = 0, node_count = 0;
	int votes=0;
	unsigned int object_handle;
	unsigned int node_object;
	unsigned int nodes_handle;
	unsigned int find_handle;

	/* Cluster name */
	if (objdb_get_string(corosync, cluster_parent_handle, "name", &cname)) {
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

	/* Sum node votes for expected. Even if we already know expected_votes, we need vote_sum
	   later */
	nodes_handle = nodeslist_init(corosync, cluster_parent_handle, &find_handle);
	do {
		int votes;

		node_count++;

		objdb_get_int(corosync, nodes_handle, "votes", (unsigned int *)&votes, 1);
		if (votes < 0) {
			log_printf(LOG_ERR, "negative votes not allowed");
			write_cman_pipe("Found negative votes for this node in CCS");
			error = -EINVAL;
			goto out;
		}
		vote_sum += votes;
		nodes_handle = nodeslist_next(corosync, find_handle);
	} while (nodes_handle);
	corosync->object_find_destroy(find_handle);

	if (expected_votes == 0) {
		corosync->object_find_create(cluster_parent_handle, "cman", strlen("cman"), &find_handle);
		if (corosync->object_find_next(find_handle, &object_handle) == 0)
		{

			/* optional expected_votes supercedes vote sum */
			objdb_get_int(corosync, object_handle, "expected_votes", (unsigned int *)&expected_votes, 0);
			if (!expected_votes)
				expected_votes = vote_sum;
		}
		corosync->object_find_destroy(find_handle);
	}

	/* find our own number of votes */
	if (getenv("CMAN_VOTES")) {
		votes = atoi(getenv("CMAN_VOTES"));
		log_printf(LOG_INFO, "Using override votes %d\n", votes);
	}

	node_object = nodelist_byname(corosync, cluster_parent_handle, our_nodename);
	if (!node_object) {
		log_printf(LOG_ERR, "unable to find votes for %s", our_nodename);
		write_cman_pipe("Unable to find votes for node in CCS");
		return -E2BIG;
	}

	if (!votes) {
		unsigned int votestmp=-1;
		objdb_get_int(corosync, node_object, "votes", &votestmp, 1);
		if (votestmp < 0 || votestmp > 255) {
			log_printf(LOG_ERR, "invalid votes value %d", votestmp);
			write_cman_pipe("Found invalid votes for node in CCS");
			return -EINVAL;
		}
		votes = votestmp;
	}
	our_votes = votes;

	/* nodeid */
	if (getenv("CMAN_NODEID")) {
		nodeid = atoi(getenv("CMAN_NODEID"));
		log_printf(LOG_INFO, "Using override nodeid %d\n", nodeid);
	}

	if (!nodeid) {
		objdb_get_int(corosync, node_object, "nodeid", (unsigned int *)&nodeid, 0);
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
int read_cman_config(struct corosync_api_v1 *corosync, unsigned int *config_version)
{
	int error;
	unsigned int find_handle;

	/* Get the parent object handle */
	corosync->object_find_create(OBJECT_PARENT_HANDLE,
				    "cluster", strlen("cluster"), &find_handle);

	corosync->object_find_next(find_handle, &cluster_parent_handle);
	corosync->object_find_destroy(find_handle);

	read_cman_nodes(corosync, config_version, 1);
	error = get_cman_join_info(corosync);
	if (error) {
		log_printf(LOG_ERR, "Error reading configuration, cannot start");
		return error;
	}

	error = join(corosync);

	return error;
}
