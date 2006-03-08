
/* Interface with openais's cman API */

#include "gd_internal.h"
#include "libcman.h"

static cman_handle_t	ch;
static cman_node_t	cman_nodes[MAX_NODES];
static int		cman_node_count;
static int		member_cb;
static int		member_reason;


int is_cman_member(int nodeid)
{
	int i;

	for (i = 0; i < cman_node_count; i++) {
		if (cman_nodes[i].cn_nodeid == nodeid)
			return cman_nodes[i].cn_member;
	}
	return 0;
}

static void process_membership_cb(void)
{
	int rv;

	cman_quorate = cman_is_quorate(ch);

	cman_node_count = 0;
	memset(&cman_nodes, 0, sizeof(cman_nodes));

	rv = cman_get_nodes(ch, MAX_NODES, &cman_node_count, cman_nodes);
	if (rv < 0) {
		log_print("cman_get_nodes error %d %d", rv, errno);
		return;
	}
}

static void membership_cb(cman_handle_t h, void *private, int reason, int arg)
{
	log_debug("member callback reason %d", reason);
	member_cb = 1;
	member_reason = reason;
}

static void process_cman(int ci)
{
	int rv = 0;

	while (1) {
		cman_dispatch(ch, CMAN_DISPATCH_ONE);

		if (member_cb) {
			member_cb = 0;
			process_membership_cb();
			rv = 1;
		} else
			break;
	}
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

	rv = cman_start_notification(ch, membership_cb);
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

	our_nodeid = node.cn_nodeid;
	log_debug("our nodeid %d", our_nodeid);

	fd = cman_get_fd(ch);
	client_add(fd, process_cman);

	rv = 0;
 out:
	return rv;
}

