/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**  
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "sm.h"
#include "cnxman-private.h"

void copy_to_usernode(struct cluster_node *node, struct cl_cluster_node *unode);

#define UST_REGISTER	1
#define UST_UNREGISTER	2
#define UST_JOIN	3
#define UST_LEAVE	4
#define UST_JOINED	5

struct event {
	struct list_head 	list;
	service_event_t		type;
	service_start_t		start_type;
	unsigned int		event_id;
	unsigned int		last_stop;
	unsigned int		last_start;
	unsigned int		last_finish;
	unsigned int		node_count;
	uint32_t *		nodeids;
};
typedef struct event event_t;

struct user_service {
	uint32_t		local_id;
	pid_t			pid;
	int			signal;
	struct socket *		sock;
	uint8_t			state;
	uint8_t			async;
	struct semaphore	lock;
	struct list_head	events;
	spinlock_t		event_lock;
	unsigned int		last_stop;
	unsigned int		last_start;
	unsigned int		last_finish;
	unsigned int		need_startdone;
	unsigned int		node_count;
	uint32_t *		nodeids;
	int			name_len;
	char			name[MAX_SERVICE_NAME_LEN];
};
typedef struct user_service user_service_t;


static void add_event(user_service_t *us, event_t *ev)
{
	spin_lock(&us->event_lock);
	list_add_tail(&ev->list, &us->events);

	switch(ev->type) {
	case SERVICE_EVENT_STOP:
		us->last_stop = us->last_start;
		break;
	case SERVICE_EVENT_START:
		us->last_start = ev->event_id;
		break;
	case SERVICE_EVENT_FINISH:
		us->last_finish = ev->event_id;
		break;
	case SERVICE_EVENT_LEAVEDONE:
		break;
	}
	spin_unlock(&us->event_lock);
}

static event_t *get_event(user_service_t *us)
{
	event_t *ev = NULL;

	spin_lock(&us->event_lock);
	if (!list_empty(&us->events)) {
		ev = list_entry(us->events.next, event_t, list);
		ev->last_stop = us->last_stop;
		ev->last_start = us->last_start;
		ev->last_finish = us->last_finish;
	}
	spin_unlock(&us->event_lock);
	return ev;
}

static void del_event(user_service_t *us, event_t *ev)
{
	spin_lock(&us->event_lock);
	list_del(&ev->list);
	spin_unlock(&us->event_lock);
}

static event_t *alloc_event(void)
{
	event_t *ev;
	SM_RETRY(ev = (event_t *) kmalloc(sizeof(event_t), GFP_KERNEL), ev);
	memset(ev, 0, sizeof(event_t));
	return ev;
}

/* us->lock must be held before calling */
static void user_notify(user_service_t *us)
{
	if (us->sock)
		queue_oob_skb(us->sock, CLUSTER_OOB_MSG_SERVICEEVENT);
	if (us->pid && us->signal)
		kill_proc(us->pid, us->signal, 0);
}

static service_start_t start_type(int type)
{
	switch (type) {
	case SERVICE_NODE_FAILED:
		return SERVICE_START_FAILED;
	case SERVICE_NODE_JOIN:
		return SERVICE_START_JOIN;
	case SERVICE_NODE_LEAVE:
		return SERVICE_START_LEAVE;
	}
	return 0;
}

static int user_stop(void *servicedata)
{
	user_service_t *us = (user_service_t *) servicedata;
	event_t *ev;

	down(&us->lock);
	if (!us->sock)
		goto out;

	ev = alloc_event();
	ev->type = SERVICE_EVENT_STOP;

	add_event(us, ev);
	user_notify(us);
 out:
	up(&us->lock);
	return 0;
}

static int user_start(void *servicedata, uint32_t *nodeids, int count,
		      int event_id, int type)
{
	user_service_t *us = (user_service_t *) servicedata;
	event_t *ev;

	down(&us->lock);
	if (!us->sock) {
		kcl_start_done(us->local_id, event_id);
		goto out;
	}

	us->need_startdone = event_id;

	ev = alloc_event();
	ev->type = SERVICE_EVENT_START;
	ev->node_count = count;
	ev->start_type = start_type(type);
	ev->event_id = event_id;
	ev->nodeids = nodeids;

	add_event(us, ev);
	user_notify(us);
 out:
	up(&us->lock);
	return 0;
}

static void user_finish(void *servicedata, int event_id)
{
	user_service_t *us = (user_service_t *) servicedata;
	event_t *ev;

	down(&us->lock);
	if (!us->sock)
		goto out;

	ev = alloc_event();
	ev->type = SERVICE_EVENT_FINISH;
	ev->event_id = event_id;

	add_event(us, ev);
	user_notify(us);
 out:
	up(&us->lock);
}

struct kcl_service_ops user_service_ops = {
	.stop = user_stop,
	.start = user_start,
	.finish = user_finish
};

static int user_register(char *name, user_service_t **us_data)
{
	user_service_t *us;
	int len = strlen(name);
	int error;

	if (len > MAX_SERVICE_NAME_LEN - 1)
		return -ENAMETOOLONG;
	if (!len)
		return -EINVAL;

	us = kmalloc(sizeof(user_service_t), GFP_KERNEL);
	if (!us)
		return -ENOMEM;
	memset(us, 0, sizeof(user_service_t));
	us->nodeids = NULL;
	INIT_LIST_HEAD(&us->events);
	spin_lock_init(&us->event_lock);
	init_MUTEX(&us->lock);
	us->name_len = len;
	memcpy(us->name, name, len);

	error = kcl_register_service(name, len, SERVICE_LEVEL_USER,
				     &user_service_ops, TRUE, (void *) us,
				     &us->local_id);
	if (error) {
		kfree(us);
		us = NULL;
	}
	*us_data = us;
	return error;
}

static void user_unregister(user_service_t *us)
{
	event_t *ev;

	kcl_unregister_service(us->local_id);

	if (us->nodeids)
		kfree(us->nodeids);

	while ((ev = get_event(us))) {
		del_event(us, ev);
		if (ev->nodeids)
			kfree(ev->nodeids);
		kfree(ev);
	}
}

static int user_join_async(void *arg)
{
	user_service_t *us = arg;
	int user_gone = 0;

	daemonize("cman_userjoin");

	kcl_join_service(us->local_id);

	down(&us->lock);
	us->state = UST_JOINED;
	us->async = 0;
	if (!us->sock) {
		if (us->need_startdone)
			kcl_start_done(us->local_id, us->need_startdone);
		user_gone = 1;
	}
	up(&us->lock);

	if (user_gone) {
		kcl_leave_service(us->local_id);
		user_unregister(us);
		kfree(us);
	}
	return 0;
}

static int user_leave_async(void *arg)
{
	user_service_t *us = arg;

	daemonize("cman_userleave");

	kcl_leave_service(us->local_id);

	down(&us->lock);
	us->async = 0;
	if (!us->sock) {
		user_unregister(us);
		kfree(us);
	} else {
		event_t *ev = alloc_event();
		ev->type = SERVICE_EVENT_LEAVEDONE;
		add_event(us, ev);
		user_notify(us);
		up(&us->lock);
	}

	return 0;
}

static int user_join(user_service_t *us, int wait)
{
	int error = 0;

	if (wait) {
		error = kcl_join_service(us->local_id);
		us->state = UST_JOINED;
	}
	else {
		us->async = 1;
		kernel_thread(user_join_async, us, 0);
	}

	return error;
}

static void user_leave(user_service_t *us, int wait)
{
	if (wait)
		kcl_leave_service(us->local_id);
	else {
		us->async = 1;
		kernel_thread(user_leave_async, us, 0);
	}
}

static int user_start_done(user_service_t *us, unsigned int event_id)
{
	if (!us->need_startdone)
		return -EINVAL;
	if (us->need_startdone == event_id)
		us->need_startdone = 0;
	kcl_start_done(us->local_id, event_id);
	return 0;
}

static void user_set_signal(user_service_t *us, int signal)
{
	us->pid = current->pid;
	us->signal = signal;
}

static int user_get_event(user_service_t *us,
			  struct cl_service_event *user_event)
{
	event_t *ev;
	struct cl_service_event event;

	ev = get_event(us);
	if (!ev)
		return 0;

	event.type        = ev->type;
	event.start_type  = ev->start_type;
	event.event_id	  = ev->event_id;
	event.last_stop	  = ev->last_stop;
	event.last_start  = ev->last_start;
	event.last_finish = ev->last_finish;
	event.node_count  = ev->node_count;

	if (copy_to_user(user_event, &event, sizeof(struct cl_service_event)))
		return -EFAULT;

	del_event(us, ev);

	if (ev->type == SERVICE_EVENT_START) {
		if (us->nodeids)
			kfree(us->nodeids);
		us->nodeids = ev->nodeids;
		us->node_count = ev->node_count;
	}

	kfree(ev);
	return 1;
}

static int user_get_members(user_service_t *us,
			    struct cl_cluster_nodelist *u_nodelist)
{
	struct cl_cluster_nodelist user_nodelist;
	struct cl_cluster_node user_node, *u_node;
	struct cluster_node *node;
	unsigned int i;
	int num_nodes = 0;

	if (!u_nodelist)
		return us->node_count;

	if (copy_from_user(&user_nodelist, (void __user *) u_nodelist,
			   sizeof(struct cl_cluster_nodelist)))
		return -EFAULT;

	if (user_nodelist.max_members < us->node_count)
		return -E2BIG;

	u_node = user_nodelist.nodes;

	for (i = 0; i < us->node_count; i++) {
		node = find_node_by_nodeid(us->nodeids[i]);
		if (!node)
			continue;

		copy_to_usernode(node, &user_node);
		if (copy_to_user(u_node, &user_node,
				 sizeof(struct cl_cluster_node)))
			return -EFAULT;

		u_node++;
		num_nodes++;
	}
	return num_nodes;
}

static int user_global_id(user_service_t *us, uint32_t *id)
{
	uint32_t gid = 0;

	if (us->state != UST_JOINED)
		return -EINVAL;

	kcl_global_service_id(us->local_id, &gid);

	if (copy_to_user(id, &gid, sizeof(uint32_t)))
		return -EFAULT;
	return 0;
}

static int user_set_level(user_service_t *us, int level)
{
	int prev_id = us->local_id;
	int error;

	if (us->state != UST_REGISTER)
		return -EINVAL;

	error = kcl_register_service(us->name, us->name_len, level,
				     &user_service_ops, TRUE, (void *) us,
				     &us->local_id);
	if (error)
		return error;

	kcl_unregister_service(prev_id);
	return 0;
}

int sm_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct cluster_sock *c = cluster_sk(sock->sk);
	user_service_t *us = c->service_data;
	int error = 0;

	if (!us && cmd != SIOCCLUSTER_SERVICE_REGISTER)
		return -EINVAL;

	switch (cmd) {
	case SIOCCLUSTER_SERVICE_REGISTER:
		error = user_register((char *) arg, &us);
		if (!error) {
			us->state = UST_REGISTER;
			us->sock = sock;
			c->service_data = us;
		}
		break;

	case SIOCCLUSTER_SERVICE_UNREGISTER:
		down(&us->lock);
		us->state = UST_UNREGISTER;
		user_unregister(us);
		up(&us->lock);
		break;

	case SIOCCLUSTER_SERVICE_JOIN:
		us->state = UST_JOIN;
		user_join(us, 0);
		break;

	case SIOCCLUSTER_SERVICE_LEAVE:
		down(&us->lock);
		if (us->state != UST_JOINED) {
			error = -EBUSY;
			up(&us->lock);
		} else {
			us->state = UST_LEAVE;
			up(&us->lock);
			user_leave(us, 0);
		}
		break;

	case SIOCCLUSTER_SERVICE_SETSIGNAL:
		user_set_signal(us, (int) arg);
		break;

	case SIOCCLUSTER_SERVICE_STARTDONE:
		error = user_start_done(us, (unsigned int) arg);
		break;

	case SIOCCLUSTER_SERVICE_GETEVENT:
		error = user_get_event(us, (struct cl_service_event *) arg);
		break;

	case SIOCCLUSTER_SERVICE_GETMEMBERS:
		error = user_get_members(us, (struct cl_cluster_nodelist *)arg);
		break;

	case SIOCCLUSTER_SERVICE_GLOBALID:
		error = user_global_id(us, (uint32_t *) arg);
		break;

	case SIOCCLUSTER_SERVICE_SETLEVEL:
		error = user_set_level(us, (int) arg);
		break;

	default:
		error = -EINVAL;
	}

	return error;
}

void sm_sock_release(struct socket *sock)
{
	struct cluster_sock *c = cluster_sk(sock->sk);
	user_service_t *us = c->service_data;
	int state;

	if (!us)
		return;

	down(&us->lock);
	us->sock = NULL;
	c->service_data = NULL;

	if (us->need_startdone)
		kcl_start_done(us->local_id, us->need_startdone);

	if (us->async) {
		/* async thread will clean up before exiting */
		up(&us->lock);
		return;
	}
	state = us->state;
	up(&us->lock);

	switch (state) {
	case UST_JOIN:
		break;
	case UST_JOINED:
		user_leave(us, 1);
		/* fall through */
	case UST_LEAVE:
	case UST_REGISTER:
		user_unregister(us);
		/* fall through */
	case UST_UNREGISTER:
		kfree(us);
		break;
	}
}
