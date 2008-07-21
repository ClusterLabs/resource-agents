#include "gfs_daemon.h"
#include "config.h"
#include <libcman.h>

static cman_handle_t ch;
static cman_cluster_t cluster;

static void cman_callback(cman_handle_t h, void *private, int reason, int arg)
{
	if (reason == CMAN_REASON_TRY_SHUTDOWN) {
		if (list_empty(&mountgroups))
			cman_replyto_shutdown(ch, 1);
		else {
			log_debug("no to cman shutdown");
			cman_replyto_shutdown(ch, 0);
		}
	}
}

void process_cman(int ci)
{
	int rv;

	rv = cman_dispatch(ch, CMAN_DISPATCH_ALL);
	if (rv == -1 && errno == EHOSTDOWN)
		cluster_dead(0);
}

int setup_cman(void)
{
	cman_node_t node;
	int rv, fd;
	int init = 0, active = 0;

 retry_init:
	ch = cman_init(NULL);
	if (!ch) {
		if (init++ < 2) {
			sleep(1);
			goto retry_init;
		}
		log_error("cman_init error %d", errno);
		return -ENOTCONN;
	}

 retry_active:
	rv = cman_is_active(ch);
	if (!rv) {
		if (active++ < 2) {
			sleep(1);
			goto retry_active;
		}
		log_error("cman_is_active error %d", errno);
		cman_finish(ch);
		return -ENOTCONN;
	}

	rv = cman_start_notification(ch, cman_callback);
	if (rv < 0) {
		log_error("cman_start_notification error %d %d", rv, errno);
		cman_finish(ch);
		return rv;
	}

	fd = cman_get_fd(ch);

	/* FIXME: wait here for us to be a member of the cluster */
	memset(&cluster, 0, sizeof(cluster));
	rv = cman_get_cluster(ch, &cluster);
	if (rv < 0) {
		log_error("cman_get_cluster error %d %d", rv, errno);
		cman_stop_notification(ch);
		cman_finish(ch);
		return rv;
	}
	clustername = cluster.ci_name;

	memset(&node, 0, sizeof(node));
	rv = cman_get_node(ch, CMAN_NODEID_US, &node);
	if (rv < 0) {
		log_error("cman_get_node error %d %d", rv, errno);
		cman_stop_notification(ch);
		cman_finish(ch);
		fd = rv;
		goto out;
	}
	our_nodeid = node.cn_nodeid;
 out:
	return fd;
}

void close_cman(void)
{
	cman_finish(ch);
}

