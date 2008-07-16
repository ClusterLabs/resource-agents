#include "fd.h"

void free_node_list(struct list_head *head)
{
	struct node *node;

	while (!list_empty(head)) {
		node = list_entry(head->next, struct node, list);
		list_del(&node->list);
		free(node);
	}
}

void add_complete_node(struct fd *fd, int nodeid)
{
	struct node *node;

	node = get_new_node(fd, nodeid);
	list_add(&node->list, &fd->complete);
}

int list_count(struct list_head *head)
{
	struct list_head *tmp;
	int count = 0;

	list_for_each(tmp, head)
		count++;
	return count;
}

int is_victim(struct fd *fd, int nodeid)
{
	struct node *node;

	list_for_each_entry(node, &fd->victims, list) {
		if (node->nodeid == nodeid)
			return 1;
	}
	return 0;
}

static void victim_done(struct fd *fd, int victim, int how)
{
	if (group_mode == GROUP_LIBGROUP)
		return;

	node_history_fence(fd, victim, our_nodeid, how, time(NULL));
	send_victim_done(fd, victim);
}

/* This routine should probe other indicators to check if victims
   can be reduced.  Right now we just check if the victim has rejoined the
   cluster. */

static int reduce_victims(struct fd *fd)
{
	struct node *node, *safe;
	int num_victims;

	num_victims = list_count(&fd->victims);

	list_for_each_entry_safe(node, safe, &fd->victims, list) {
		if (is_cman_member(node->nodeid)) {
			log_debug("reduce victim %s", node->name);
			victim_done(fd, node->nodeid, VIC_DONE_MEMBER);
			list_del(&node->list);
			free(node);
			num_victims--;
		}
	}

	return num_victims;
}

static inline void close_override(int *fd, char *path)
{
	unlink(path);
	if (fd) {
		if (*fd >= 0)
			close(*fd);
		*fd = -1;
	}
}

static int open_override(char *path)
{
	int ret;
	mode_t om;

	om = umask(077);
	ret = mkfifo(path, (S_IRUSR | S_IWUSR));
	umask(om);

	if (ret < 0)
		return -1;
        return open(path, O_RDONLY | O_NONBLOCK);
}

static int check_override(int ofd, char *nodename, int timeout)
{
	char buf[128];
	fd_set rfds;
	struct timeval tv = {0, 0};
	int ret, x;

	if (ofd < 0 || !nodename || !strlen(nodename)) {
		sleep(timeout);
		return 0;
	}

	FD_ZERO(&rfds);
	FD_SET(ofd, &rfds);
	tv.tv_usec = 0;
	tv.tv_sec = timeout;

	ret = select(ofd + 1, &rfds, NULL, NULL, &tv);
	if (ret < 0) {
		log_debug("check_override select: %s", strerror(errno));
		return -1;
	}

	if (ret == 0)
		return 0;

	memset(buf, 0, sizeof(buf));
	ret = read(ofd, buf, sizeof(buf) - 1);
	if (ret < 0) {
		log_debug("check_override read: %s", strerror(errno));
		return -1;
	}

	/* chop off control characters */
	for (x = 0; x < ret; x++) {
		if (buf[x] < 0x20) {
			buf[x] = 0;
			break;
		}
	}

	if (!strcasecmp(nodename, buf)) {
		/* Case insensitive, but not as nice as, say, name_equal
		   in the other file... */
		return 1;
	}

	return 0;
}

/* If there are victims after a node has joined, it's a good indication that
   they may be joining the cluster shortly.  If we delay a bit they might
   become members and we can avoid fencing them.  This is only really an issue
   when the fencing method reboots the victims.  Otherwise, the nodes should
   unfence themselves when they start up. */

void delay_fencing(struct fd *fd, int node_join)
{
	struct timeval first, last, start, now;
	int victim_count, last_count = 0, delay = 0;
	struct node *node;
	char *delay_type;

	if (list_empty(&fd->victims))
		return;

	if (node_join) {
		delay = comline.post_join_delay;
		delay_type = "post_join_delay";
	} else {
		delay = comline.post_fail_delay;
		delay_type = "post_fail_delay";
	}

	if (delay == 0)
		goto out;

	gettimeofday(&first, NULL);
	gettimeofday(&start, NULL);

	query_unlock();
	for (;;) {
		sleep(1);

		victim_count = reduce_victims(fd);

		if (victim_count == 0)
			break;

		if (victim_count < last_count) {
			gettimeofday(&start, NULL);
			if (delay > 0 && comline.post_join_delay > delay) {
				delay = comline.post_join_delay;
				delay_type = "post_join_delay (modified)";
			}
		}

		last_count = victim_count;

		/* negative delay means wait forever */
		if (delay == -1)
			continue;

		gettimeofday(&now, NULL);
		if (now.tv_sec - start.tv_sec >= delay)
			break;
	}
	query_lock();

	gettimeofday(&last, NULL);

	log_debug("delay of %ds leaves %d victims",
		  (int) (last.tv_sec - first.tv_sec), victim_count);
 out:
	list_for_each_entry(node, &fd->victims, list) {
		log_debug("%s not a cluster member after %d sec %s",
		          node->name, delay, delay_type);
	}
}

void defer_fencing(struct fd *fd)
{
	char *master_name;

	if (list_empty(&fd->victims))
		return;

	master_name = nodeid_to_name(fd->master);

	log_level(LOG_INFO, "fencing deferred to %s", master_name);
}

void fence_victims(struct fd *fd)
{
	struct node *node;
	int error;
	int override = -1;
	int member, fenced;

	while (!list_empty(&fd->victims)) {
		node = list_entry(fd->victims.next, struct node, list);

		member = is_cman_member(node->nodeid);
		if (group_mode == GROUP_LIBCPG)
			fenced = is_fenced_external(fd, node->nodeid);
		else
			fenced = 0;

		if (member || fenced) {
			log_debug("averting fence of node %s "
				  "member %d external %d",
				  node->name, member, fenced);
			victim_done(fd, node->nodeid, member ? VIC_DONE_MEMBER :
							       VIC_DONE_EXTERNAL);
			list_del(&node->list);
			free(node);
			continue;
		}

		log_level(LOG_INFO, "fencing node \"%s\"", node->name);

		query_unlock();
		error = fence_node(node->name);
		query_lock();

		log_level(LOG_INFO, "fence \"%s\" %s", node->name,
			  error ? "failed" : "success");

		if (!error) {
			victim_done(fd, node->nodeid, VIC_DONE_AGENT);
			list_del(&node->list);
			free(node);
			continue;
		}

		if (!comline.override_path) {
			query_unlock();
			sleep(5);
			query_lock();
			continue;
		}

		query_unlock();
		/* Check for manual intervention */
		override = open_override(comline.override_path);
		if (check_override(override, node->name,
				   comline.override_time) > 0) {
			log_level(LOG_WARNING, "fence \"%s\" overridden by "
				  "administrator intervention", node->name);
			victim_done(fd, node->nodeid, VIC_DONE_OVERRIDE);
			list_del(&node->list);
			free(node);
		}
		close_override(&override, comline.override_path);
		query_lock();
	}
}

