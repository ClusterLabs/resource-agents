#include "fd.h"
#include "libgroup.h"

#define DO_STOP 1
#define DO_START 2
#define DO_FINISH 3
#define DO_TERMINATE 4
#define DO_SETID 5

#define GROUPD_TIMEOUT 10 /* seconds */

/* save all the params from callback functions here because we can't
   do the processing within the callback function itself */

static group_handle_t gh;
static int cb_action;
static char cb_name[MAX_GROUPNAME_LEN+1];
static int cb_event_nr;
static int cb_id;
static int cb_type;
static int cb_member_count;
static int cb_members[MAX_NODES];


static void stop_cbfn(group_handle_t h, void *private, char *name)
{
	cb_action = DO_STOP;
	strcpy(cb_name, name);
}

static void start_cbfn(group_handle_t h, void *private, char *name,
		       int event_nr, int type, int member_count, int *members)
{
	int i;

	cb_action = DO_START;
	strcpy(cb_name, name);
	cb_event_nr = event_nr;
	cb_type = type;
	cb_member_count = member_count;

	for (i = 0; i < member_count; i++)
		cb_members[i] = members[i];
}

static void finish_cbfn(group_handle_t h, void *private, char *name,
			int event_nr)
{
	cb_action = DO_FINISH;
	strcpy(cb_name, name);
	cb_event_nr = event_nr;
}

static void terminate_cbfn(group_handle_t h, void *private, char *name)
{
	cb_action = DO_TERMINATE;
	strcpy(cb_name, name);
}

static void setid_cbfn(group_handle_t h, void *private, char *name,
		       int unsigned id)
{
	cb_action = DO_SETID;
	strcpy(cb_name, name);
	cb_id = id;
}

group_callbacks_t callbacks = {
	stop_cbfn,
	start_cbfn,
	finish_cbfn,
	terminate_cbfn,
	setid_cbfn
};

static char *str_members(void)
{
	static char mbuf[MAXLINE];
	int i, len = 0;

	memset(mbuf, 0, MAXLINE);

	for (i = 0; i < cb_member_count; i++)
		len += sprintf(mbuf+len, "%d ", cb_members[i]);
	return mbuf;
}

static int id_in_nodeids(int nodeid, int count, int *nodeids)
{
	int i;

	for (i = 0; i < count; i++) {
		if (nodeid == nodeids[i])
			return 1;
	}
	return 0;
}

static int next_complete_nodeid(struct fd *fd, int gt)
{
	struct node *node;
	int low = -1;

	/* find lowest node id in fd_complete greater than gt,
	   if none, return -1 */

	list_for_each_entry(node, &fd->complete, list) {
		if (node->nodeid <= gt)
			continue;

		if (low == -1)
			low = node->nodeid;
		else if (node->nodeid < low)
			low = node->nodeid;
	}
	return low;
}

static void set_master(struct fd *fd)
{
	struct node *node;
	int low = -1;

	/* Find the lowest nodeid common to fd->fd_prev (newest member list)
	 * and fd->fd_complete (last complete member list). */

	for (;;) {
		low = next_complete_nodeid(fd, low);
		if (low == -1)
			break;

		list_for_each_entry(node, &fd->prev, list) {
			if (low != node->nodeid)
				continue;
			goto out;
		}
	}

	/* Special case: we're the first and only FD member */

	if (fd->prev_count == 1)
		low = our_nodeid;

	/* We end up returning -1 when we're not the only node and we've just
	   joined.  Because we've just joined we weren't in the last complete
	   domain group and won't be chosen as master.  We defer to someone who
	   _was_ in the last complete group.  All we know is it isn't us. */

 out:
	fd->master = low;
}

static void new_prev_nodes(struct fd *fd, int member_count, int *nodeids)
{
	struct node *node;
	int i;

	for (i = 0; i < member_count; i++) {
		node = get_new_node(fd, nodeids[i]);
		list_add(&node->list, &fd->prev);
	}

	fd->prev_count = member_count;
}

static void _add_first_victims(struct fd *fd)
{
	struct node *prev_node, *safe;

	/* complete list initialised in init_nodes() to all nodes from ccs */
	if (list_empty(&fd->complete))
		log_debug("first complete list empty warning");

	list_for_each_entry_safe(prev_node, safe, &fd->complete, list) {
		if (!is_cman_member(prev_node->nodeid)) {
			list_del(&prev_node->list);
			list_add(&prev_node->list, &fd->victims);
			log_debug("add first victim %s", prev_node->name);
			prev_node->init_victim = 1;
		}
	}
}

static void _add_victims(struct fd *fd, int start_type, int member_count,
			 int *nodeids)
{
	struct node *node, *safe;

	/* nodes which haven't completed leaving when a failure restart happens
	 * are dead (and need fencing) or are still members */

	if (start_type == GROUP_NODE_FAILED) {
		list_for_each_entry_safe(node, safe, &fd->leaving, list) {
			list_del(&node->list);
			if (id_in_nodeids(node->nodeid, member_count, nodeids))
				list_add(&node->list, &fd->complete);
			else {
				list_add(&node->list, &fd->victims);
				log_debug("add victim %u, was leaving",
					  node->nodeid);
			}
		}
	}

	/* nodes in last completed group but missing from fr_nodeids are added
	 * to victims list or leaving list, depending on the type of start. */

	if (list_empty(&fd->complete))
		log_debug("complete list empty warning");

	list_for_each_entry_safe(node, safe, &fd->complete, list) {
		if (!id_in_nodeids(node->nodeid, member_count, nodeids)) {
			list_del(&node->list);

			if (start_type == GROUP_NODE_FAILED)
				list_add(&node->list, &fd->victims);
			else
				list_add(&node->list, &fd->leaving);

			log_debug("add node %u to list %u", node->nodeid,
				  start_type);
		}
	}
}

static void add_victims(struct fd *fd, int start_type, int member_count,
			int *nodeids)
{
	/* Reset things when the last stop aborted our first
	 * start, i.e. there was no finish; we got a
	 * start/stop/start immediately upon joining. */

	if (!fd->last_finish && fd->last_stop) {
		log_debug("revert aborted first start");
		fd->last_stop = 0;
		fd->first_recovery = 0;
		free_node_list(&fd->prev);
		free_node_list(&fd->victims);
		free_node_list(&fd->leaving);
	}

	log_debug("add_victims stop %d start %d finish %d",
		  fd->last_stop, fd->last_start, fd->last_finish);

	if (!fd->first_recovery) {
		fd->first_recovery = 1;
		_add_first_victims(fd);
	} else
		_add_victims(fd, start_type, member_count, nodeids);

	/* "prev" is just a temporary list of node structs matching the list of
	   nodeids from the start; these nodes are moved to the "complete" list
	   in the finish callback, and will be used to compare against the
	   next set of started nodes */
	   
	free_node_list(&fd->prev);
	new_prev_nodes(fd, member_count, nodeids);
}

static void clear_victims(struct fd *fd)
{
	struct node *node, *safe;

	if (fd->last_finish == fd->last_start) {
		free_node_list(&fd->leaving);
		free_node_list(&fd->victims);
	}

	/* Save a copy of this set of nodes which constitutes the latest
	 * complete group.  Any of these nodes missing in the next start will
	 * either be leaving or victims.  For the next recovery, the lowest
	 * remaining nodeid in this group will be the master. */

	free_node_list(&fd->complete);
	list_for_each_entry_safe(node, safe, &fd->prev, list) {
		list_del(&node->list);
		list_add(&node->list, &fd->complete);
	}
}

void process_groupd(int ci)
{
	struct fd *fd;
	int error = -EINVAL;

	group_dispatch(gh);

	if (!cb_action)
		goto out;

	fd = find_fd(cb_name);
	if (!fd)
		goto out;

	switch (cb_action) {
	case DO_STOP:
		log_debug("stop %s", cb_name);
		fd->last_stop = fd->last_start;
		group_stop_done(gh, cb_name);
		break;

	case DO_START:
		log_debug("start %s %d members %s", cb_name, cb_event_nr,
			  str_members());
		fd->last_start = cb_event_nr;

		/* we don't get a start callback until there's quorum */

		add_victims(fd, cb_type, cb_member_count, cb_members);
		set_master(fd);
		if (fd->master == our_nodeid) {
			delay_fencing(fd, cb_type == GROUP_NODE_JOIN);
			fence_victims(fd);
		} else {
			defer_fencing(fd);
		}

		group_start_done(gh, cb_name, cb_event_nr);
		fd->joining_group = 0;
		break;

	case DO_FINISH:
		log_debug("finish %s %d", cb_name, cb_event_nr);
		fd->last_finish = cb_event_nr;

		/* we get terminate callback when all have started, which means
		   that the low node has successfully fenced all victims */
		clear_victims(fd);

		break;

	case DO_TERMINATE:
		log_debug("terminate %s", cb_name);
		if (!fd->leaving_group)
			log_error("process_groupd terminate not leaving");
		list_del(&fd->list);
		free_fd(fd);
		break;

	case DO_SETID:
		break;
	default:
		error = -EINVAL;
	}

	cb_action = 0;
 out:
	return;
}

int setup_groupd(void)
{
	int rv;

	gh = group_init(NULL, "fence", 0, &callbacks, GROUPD_TIMEOUT);
	if (!gh) {
		log_error("group_init error %p %d", gh, errno);
		return -ENOTCONN;
	}
	rv = group_get_fd(gh);
	if (rv < 0)
		log_error("group_get_fd error %d %d", rv, errno);
	return rv;
}

void close_groupd(void)
{
	group_exit(gh);
}

int fd_join_group(struct fd *fd)
{
	int rv;

	list_add(&fd->list, &domains);
	fd->joining_group = 1;

	rv = group_join(gh, fd->name);
	if (rv) {
		log_error("group_join error %d", rv);
		list_del(&fd->list);
		free(fd);
	}
	return rv;
}

int fd_leave_group(struct fd *fd)
{
	int rv;

	fd->leaving_group = 1;

	rv = group_leave(gh, fd->name);
	if (rv)
		log_error("group_leave error %d", rv);

	return rv;
}

int set_node_info_group(struct fd *fd, int nodeid, struct fenced_node *nodeinfo)
{
	nodeinfo->nodeid = nodeid;
	nodeinfo->victim = is_victim(fd, nodeid);
	nodeinfo->member = id_in_nodeids(nodeid, cb_member_count, cb_members);

	/* FIXME: need to keep track of last fence info for nodes */

	return 0;
}

int set_domain_info_group(struct fd *fd, struct fenced_domain *domain)
{
	domain->master_nodeid = fd->master;
	domain->victim_count = list_count(&fd->victims);
	domain->member_count = cb_member_count;
	domain->state = cb_action;
	return 0;
}

int set_domain_nodes_group(struct fd *fd, int option, int *node_count,
			   struct fenced_node **nodes_out)
{
	struct fenced_node *nodes = NULL, *nodep;
	int i;

	if (!cb_member_count)
		goto out;

	nodes = malloc(cb_member_count * sizeof(struct fenced_node));
	if (!nodes)
		return -ENOMEM;

	nodep = nodes;
	for (i = 0; i < cb_member_count; i++) {
		set_node_info(fd, cb_members[i], nodep++);
	}
 out:
	*node_count = cb_member_count;
	*nodes_out = nodes;
	return 0;
}

