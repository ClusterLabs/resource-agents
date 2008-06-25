#include "fd.h"
#include <libcman.h>

#define BUFLEN		MAX_NODENAME_LEN+1

static cman_handle_t	ch;
static cman_node_t	cman_nodes[MAX_NODES];
static int		cman_node_count;


static int name_equal(char *name1, char *name2)
{
	char name3[BUFLEN], name4[BUFLEN];
	int i, len1, len2;

	len1 = strlen(name1);
	len2 = strlen(name2);

	if (len1 == len2 && !strncmp(name1, name2, len1))
		return 1;

	memset(name3, 0, BUFLEN);
	memset(name4, 0, BUFLEN);

	for (i = 0; i < BUFLEN && i < len1; i++) {
		if (name1[i] != '.')
			name3[i] = name1[i];
		else
			break;
	}

	for (i = 0; i < BUFLEN && i < len2; i++) {
		if (name2[i] != '.')
			name4[i] = name2[i];
		else
			break;
	}

	len1 = strlen(name3);
	len2 = strlen(name4);

	if (len1 == len2 && !strncmp(name3, name4, len1))
		return 1;

	return 0;
}

static cman_node_t *find_cman_node_name(char *name)
{
	int i;

	for (i = 0; i < cman_node_count; i++) {
		if (name_equal(cman_nodes[i].cn_name, name))
			return &cman_nodes[i];
	}
	return NULL;
}

static cman_node_t *find_cman_node(int nodeid)
{
	int i;

	for (i = 0; i < cman_node_count; i++) {
		if (cman_nodes[i].cn_nodeid == nodeid)
			return &cman_nodes[i];
	}
	return NULL;
}

char *nodeid_to_name(int nodeid)
{
	cman_node_t *cn;

	cn = find_cman_node(nodeid);
	if (cn)
		return cn->cn_name;

	return "unknown";
}

int name_to_nodeid(char *name)
{
	cman_node_t *cn;

	cn = find_cman_node_name(name);
	if (cn)
		return cn->cn_nodeid;

	return -1;
}

static void statechange(void)
{
	int rv;

	cman_quorate = cman_is_quorate(ch);
	cman_node_count = 0;
	memset(&cman_nodes, 0, sizeof(cman_nodes));

	rv = cman_get_nodes(ch, MAX_NODES, &cman_node_count, cman_nodes);
	if (rv < 0)
		log_error("cman_get_nodes error %d %d", rv, errno);
}

static void cman_callback(cman_handle_t h, void *private, int reason, int arg)
{
	int quorate = cman_quorate;

	switch (reason) {
	case CMAN_REASON_TRY_SHUTDOWN:
		if (list_empty(&domains))
			cman_replyto_shutdown(ch, 1);
		else {
			log_debug("no to cman shutdown");
			cman_replyto_shutdown(ch, 0);
		}
		break;
	case CMAN_REASON_STATECHANGE:
		statechange();

		/* domain may have been waiting for quorum */
		if (!quorate && cman_quorate && (group_mode == GROUP_LIBCPG))
			process_fd_changes();
		break;

	case CMAN_REASON_CONFIG_UPDATE:
		setup_logging(&daemon_debug_logsys);
		break;
	}
}

void process_cman(int ci)
{
	int rv;

	rv = cman_dispatch(ch, CMAN_DISPATCH_ALL);
	if (rv == -1 && errno == EHOSTDOWN) {
		log_error("cluster is down, exiting");
		exit(1);
	}
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

	statechange();

	fd = cman_get_fd(ch);

	/* FIXME: wait here for us to be a member of the cluster */
	memset(&node, 0, sizeof(node));
	rv = cman_get_node(ch, CMAN_NODEID_US, &node);
	if (rv < 0) {
		log_error("cman_get_node us error %d %d", rv, errno);
		cman_finish(ch);
		fd = rv;
		goto out;
	}

	memset(our_name, 0, sizeof(our_name));
	strncpy(our_name, node.cn_name, CMAN_MAX_NODENAME_LEN);
	our_nodeid = node.cn_nodeid;

	log_debug("our_nodeid %d our_name %s", our_nodeid, our_name);
 out:
	return fd;
}

void close_cman(void)
{
	cman_finish(ch);
}

int is_cman_member(int nodeid)
{
	cman_node_t *cn;

	/* Note: in fence delay loop we aren't processing callbacks so won't
	   have done a statechange() in response to a cman callback */
	statechange();

	cn = find_cman_node(nodeid);
	if (cn && cn->cn_member)
		return 1;

	log_debug("node %d not a cman member, cn %d", nodeid, cn ? 1 : 0);
	return 0;
}

struct node *get_new_node(struct fd *fd, int nodeid)
{
	cman_node_t cn;
	struct node *node;
	int rv;

	node = malloc(sizeof(*node));
	if (!node)
		return NULL;
	memset(node, 0, sizeof(*node));

	node->nodeid = nodeid;

	memset(&cn, 0, sizeof(cn));
	rv = cman_get_node(ch, nodeid, &cn);
	if (rv < 0)
		log_debug("get_new_node %d no cman node %d", nodeid, rv);
	else
		strncpy(node->name, cn.cn_name, MAX_NODENAME_LEN);

	return node;
}

