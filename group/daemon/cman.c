
/* Interface with corosync's cman API */

#include <libcman.h>
#include "gd_internal.h"

static cman_handle_t	ch;
static cman_handle_t	ch_admin;
static int		old_quorate;
static cman_node_t	old_nodes[MAX_NODES];
static int		old_node_count;
static cman_node_t	cman_nodes[MAX_NODES];
static int		cman_node_count;
static char		name_buf[CMAN_MAX_NODENAME_LEN+1];


int kill_cman(int nodeid)
{
	return cman_kill_node(ch_admin, nodeid);
}

static int is_member(cman_node_t *node_list, int count, int nodeid)
{
	int i;

	for (i = 0; i < count; i++) {
		if (node_list[i].cn_nodeid == nodeid)
			return node_list[i].cn_member;
	}
	return 0;
}

static int is_old_member(int nodeid)
{
	return is_member(old_nodes, old_node_count, nodeid);
}

static int is_cman_member(int nodeid)
{
	return is_member(cman_nodes, cman_node_count, nodeid);
}

static void statechange(void)
{
	int i, rv;

	old_quorate = cman_quorate;
	old_node_count = cman_node_count;
	memcpy(&old_nodes, &cman_nodes, sizeof(old_nodes));

	cman_quorate = cman_is_quorate(ch);

	cman_node_count = 0;
	memset(&cman_nodes, 0, sizeof(cman_nodes));
	rv = cman_get_nodes(ch, MAX_NODES, &cman_node_count, cman_nodes);
	if (rv < 0) {
		log_print("cman_get_nodes error %d %d", rv, errno);
		return;
	}

	/*
	printf("cman: %d old nodes:\n", old_node_count);
	for (i = 0; i < old_node_count; i++)
		printf("%d:%d ", old_nodes[i].cn_nodeid,
				 old_nodes[i].cn_member);
	printf("\n");

	printf("cman: %d new nodes:\n", cman_node_count);
	for (i = 0; i < cman_node_count; i++)
		printf("%d:%d ", cman_nodes[i].cn_nodeid,
				 cman_nodes[i].cn_member);
	printf("\n");
	*/

	if (old_quorate && !cman_quorate)
		log_debug("cman: lost quorum");
	if (!old_quorate && cman_quorate)
		log_debug("cman: have quorum");

	for (i = 0; i < old_node_count; i++) {
		if (old_nodes[i].cn_member &&
		    !is_cman_member(old_nodes[i].cn_nodeid)) {

			log_debug("cman: node %d removed",
				  old_nodes[i].cn_nodeid);
			add_recovery_set_cman(old_nodes[i].cn_nodeid);
		}
	}

	for (i = 0; i < cman_node_count; i++) {
		if (cman_nodes[i].cn_member &&
		    !is_old_member(cman_nodes[i].cn_nodeid))
			log_debug("cman: node %d added",
				  cman_nodes[i].cn_nodeid);
	}
}

static void cman_callback(cman_handle_t h, void *private, int reason, int arg)
{
	switch (reason) {
	case CMAN_REASON_TRY_SHUTDOWN:
		cman_replyto_shutdown(ch, 1);
		break;
	case CMAN_REASON_STATECHANGE:
		statechange();
		break;
	case CMAN_REASON_CONFIG_UPDATE:
		setup_logging();
		break;
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
		log_print("cman_init error %d", errno);
		return -ENOTCONN;
	}

 retry_active:
	rv = cman_is_active(ch);
	if (!rv) {
		if (active++ < 2) {
			sleep(1);
			goto retry_active;
		}
		log_print("cman_is_active error %d", errno);
		cman_finish(ch);
		return -ENOTCONN;
	}

	ch_admin = cman_admin_init(NULL);
	if (!ch_admin) {
		log_print("cman_admin_init error %d", errno);
		cman_finish(ch);
		return -ENOTCONN;
	}

	rv = cman_start_notification(ch, cman_callback);
	if (rv < 0) {
		log_print("cman_start_notification error %d %d", rv, errno);
		cman_finish(ch);
		cman_finish(ch_admin);
		return rv;
	}

	memset(&node, 0, sizeof(node));
	rv = cman_get_node(ch, CMAN_NODEID_US, &node);
	if (rv < 0) {
		log_print("cman_get_node us error %d %d", rv, errno);
		cman_stop_notification(ch);
		cman_finish(ch);
		cman_finish(ch_admin);
		return rv;
	}

	cman_node_count = 0;
	memset(&cman_nodes, 0, sizeof(cman_nodes));
	rv = cman_get_nodes(ch, MAX_NODES, &cman_node_count, cman_nodes);
	if (rv < 0) {
		log_print("cman_get_nodes error %d %d", rv, errno);
		cman_stop_notification(ch);
		cman_finish(ch);
		cman_finish(ch_admin);
		return rv;
	}

	cman_quorate = cman_is_quorate(ch);

	memset(name_buf, 0, sizeof(name_buf));
	strncpy(name_buf, node.cn_name, CMAN_MAX_NODENAME_LEN);
	our_name = name_buf;
	our_nodeid = node.cn_nodeid;
	log_debug("cman: our nodeid %d name %s quorum %d",
		  our_nodeid, our_name, cman_quorate);

	fd = cman_get_fd(ch);

	return fd;
}

void close_cman(void)
{
	cman_finish(ch);
	cman_finish(ch_admin);
}

