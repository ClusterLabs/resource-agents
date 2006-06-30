
/* Interface with openais's cman API */

#include <libcman.h>
#include "gd_internal.h"

static cman_handle_t	ch;
static int		old_quorate;
static cman_node_t	old_nodes[MAX_NODES];
static int		old_node_count;
static cman_node_t	cman_nodes[MAX_NODES];
static int		cman_node_count;
static int		cman_cb;
static int		cman_reason;
static char		name_buf[CMAN_MAX_NODENAME_LEN+1];


int kill_cman(int nodeid)
{
	cman_handle_t ach;
	int rv;

	ach = cman_admin_init(NULL);
	if (!ach) {
		log_print("cman_admin_init error %d %d", (int) ch, errno);
		return -ENOTCONN;
	}
	rv = cman_kill_node(ach, nodeid);
	cman_finish(ach);
	return rv;
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
	struct recovery_set *rs;
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
			rs = get_recovery_set(old_nodes[i].cn_nodeid);
			rs->cman_update = 1;

			if (!rs->cpg_update && !in_groupd_cpg(rs->nodeid)) {
				log_debug("free recovery set %d not in cpg",
					  rs->nodeid);
				list_del(&rs->list);
				free(rs);
			} else if (rs->cpg_update && list_empty(&rs->entries)) {
				log_debug("free unused recovery set %d cman",
					  rs->nodeid);
				list_del(&rs->list);
				free(rs);
			}
		}
	}

	for (i = 0; i < cman_node_count; i++) {
		if (cman_nodes[i].cn_member &&
		    !is_old_member(cman_nodes[i].cn_nodeid))
			log_debug("cman: node %d added",
				  cman_nodes[i].cn_nodeid);
	}
}

static void process_cman_callback(void)
{
	switch (cman_reason) {
	case CMAN_REASON_STATECHANGE:
		statechange();
		break;
	default:
		break;
	}
}

static void cman_callback(cman_handle_t h, void *private, int reason, int arg)
{
	cman_cb = 1;
	cman_reason = reason;

	if (reason == CMAN_REASON_TRY_SHUTDOWN)
		cman_replyto_shutdown(ch, 1);
}

static void close_cman(int ci)
{
	log_debug("cluster is down, exiting");
	exit(1);
}

static void process_cman(int ci)
{
	int rv;

	while (1) {
		rv = cman_dispatch(ch, CMAN_DISPATCH_ONE);
		if (rv < 0)
			break;

		if (cman_cb) {
			cman_cb = 0;
			process_cman_callback();
		} else
			break;
	}

	if (rv == -1 && errno == EHOSTDOWN)
		close_cman(ci);
}

int setup_cman(void)
{
	cman_node_t node;
	int rv, fd;

	ch = cman_init(NULL);
	if (!ch) {
		log_print("cman_init error %d %d", (int) ch, errno);
		return -ENOTCONN;
	}

	rv = cman_start_notification(ch, cman_callback);
	if (rv < 0) {
		log_print("cman_start_notification error %d %d", rv, errno);
		cman_finish(ch);
		return rv;
	}

	memset(&node, 0, sizeof(node));
	rv = cman_get_node(ch, CMAN_NODEID_US, &node);
	if (rv < 0) {
		log_print("cman_get_node us error %d %d", rv, errno);
		cman_stop_notification(ch);
		cman_finish(ch);
		goto out;
	}

	cman_node_count = 0;
	memset(&cman_nodes, 0, sizeof(cman_nodes));
	rv = cman_get_nodes(ch, MAX_NODES, &cman_node_count, cman_nodes);
	if (rv < 0) {
		log_print("cman_get_nodes error %d %d", rv, errno);
		goto out;
	}

	cman_quorate = cman_is_quorate(ch);

	memset(name_buf, 0, sizeof(name_buf));
	strncpy(name_buf, node.cn_name, CMAN_MAX_NODENAME_LEN);
	our_name = name_buf;
	our_nodeid = node.cn_nodeid;
	log_debug("cman: our nodeid %d name %s quorum %d",
		  our_nodeid, our_name, cman_quorate);

	fd = cman_get_fd(ch);
	client_add(fd, process_cman, close_cman);

	rv = 0;
 out:
	return rv;
}

