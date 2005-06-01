/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-5 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/errno.h>

#include "list.h"
#include "cnxman-socket.h"
#include "cnxman-private.h"
#include "cnxman.h"
#include "daemon.h"
#include "logging.h"
#include "membership.h"
#include "config.h"
#include "barrier.h"

/* A list of all current barriers */
static struct list barrier_list;
static pthread_mutex_t barrier_list_lock;

static void send_barrier_complete_msg(struct cl_barrier *barrier)
{
	if (barrier->timeout) {
		del_timer(&barrier->timer);
		barrier->timeout = 0;
	}

	if (!barrier->client_complete) {
		send_status_return(barrier->con, CMAN_CMD_BARRIER, barrier->endreason);
		barrier->client_complete = 1;
	}
}

/* MUST be called with the barrier list lock held */
static struct cl_barrier *find_barrier(char *name)
{
	struct list *blist;
	struct cl_barrier *bar;

	list_iterate(blist, &barrier_list) {
		bar = list_item(blist, struct cl_barrier);

		if (strcmp(name, bar->name) == 0)
			return bar;
	}
	return NULL;
}

/* Do the stuff we need to do when the barrier has completed phase 1 */
static void check_barrier_complete_phase1(struct cl_barrier *barrier)
{
	if (barrier->got_nodes == ((barrier->expected_nodes != 0)
				   ? barrier->expected_nodes :
				   cluster_members)) {

		struct cl_barriermsg bmsg;

		barrier->completed_nodes++;	/* We have completed */
		barrier->phase = 2;	/* Wait for complete phase II */

		/* Send completion message, remember: we are in cnxman context
		 * and must not block */
		bmsg.cmd = CLUSTER_CMD_BARRIER;
		bmsg.subcmd = BARRIER_COMPLETE;
		strcpy(bmsg.name, barrier->name);

		P_BARRIER("Sending COMPLETE for %s\n", barrier->name);
		queue_message(NULL, (char *) &bmsg, sizeof (bmsg), NULL, 0, 0);
	}
}

/* Do the stuff we need to do when the barrier has been reached */
/* Return 1 if we deleted the barrier */
static int check_barrier_complete_phase2(struct cl_barrier *barrier, int status)
{
	P_BARRIER("check_complete_phase2 for %s\n", barrier->name);
	pthread_mutex_lock(&barrier->phase2_lock);

	P_BARRIER("check_complete_phase2 status=%d, c_nodes=%d, e_nodes=%d, state=%d\n",
		  status,barrier->completed_nodes, barrier->completed_nodes, barrier->state);

	if (barrier->state != BARRIER_STATE_COMPLETE &&
	    (status == -ETIMEDOUT ||
	     barrier->completed_nodes == ((barrier->expected_nodes != 0)
					  ? barrier->expected_nodes : cluster_members))) {

		barrier->endreason = status;

		/* Wake up listener */
		if (barrier->state == BARRIER_STATE_WAITING) {
			send_barrier_complete_msg(barrier);
		}
		barrier->state = BARRIER_STATE_COMPLETE;
	}
	pthread_mutex_unlock(&barrier->phase2_lock);

	/* Delete barrier if autodelete */
	if (barrier->flags & BARRIER_ATTR_AUTODELETE) {
		pthread_mutex_lock(&barrier_list_lock);
		list_del(&barrier->list);
		free(barrier);
		pthread_mutex_unlock(&barrier_list_lock);
		return 1;
	}

	return 0;
}

/* Called if a barrier timeout happens */
static void barrier_timer_fn(void *arg)
{
	struct cl_barrier *barrier = arg;

	P_BARRIER("Barrier timer_fn called for %s\n", barrier->name);

	/* Ignore any futher messages, they are too late. */
	barrier->phase = 0;

	/* and cause it to timeout */
	check_barrier_complete_phase2(barrier, -ETIMEDOUT);
}

/* Process BARRIER messages from other nodes */
void process_barrier_msg(struct cl_barriermsg *msg,
				struct cluster_node *node)
{
	struct cl_barrier *barrier;

	pthread_mutex_lock(&barrier_list_lock);
	barrier = find_barrier(msg->name);
	pthread_mutex_unlock(&barrier_list_lock);

	/* Ignore other peoples messages, in_transition() is needed here so
	 * that joining nodes will see their barrier messages before the
	 * we_are_a_cluster_member is set */
	if (!we_are_a_cluster_member && !in_transition())
		return;
	if (!barrier)
		return;

	P_BARRIER("Got %d for %s, from node %s\n", msg->subcmd, msg->name,
		  node ? node->name : "unknown");

	switch (msg->subcmd) {
	case BARRIER_WAIT:
		pthread_mutex_lock(&barrier->lock);
		if (barrier->phase == 0)
			barrier->phase = 1;

		if (barrier->phase == 1) {
			barrier->got_nodes++;
			check_barrier_complete_phase1(barrier);
		}
		else {
			log_msg(LOG_WARNING, "got WAIT barrier not in phase 1 %s (%d)\n",
			       msg->name, barrier->phase);

		}
		pthread_mutex_unlock(&barrier->lock);
		break;

	case BARRIER_COMPLETE:
		pthread_mutex_lock(&barrier->lock);
		barrier->completed_nodes++;

		/* First node to get all the WAIT messages sends COMPLETE, so
		 * we all complete */
		if (barrier->phase == 1) {
			barrier->got_nodes = barrier->expected_nodes;
			check_barrier_complete_phase1(barrier);
		}

		if (barrier->phase == 2) {
			/* If it was deleted (ret==1) then no need to unlock
			 * the mutex */
			if (check_barrier_complete_phase2(barrier, 0) == 1)
				return;
		}
		pthread_mutex_unlock(&barrier->lock);
		break;
	}
}



/* Barrier API */
static int barrier_register(struct connection *con, char *name, unsigned int flags, unsigned int nodes)
{
	struct cl_barrier *barrier;

	/* We are not joined to a cluster */
	if (!we_are_a_cluster_member)
		return -ENOTCONN;

	/* Must have a valid name */
	if (name == NULL || strlen(name) > MAX_BARRIER_NAME_LEN - 1)
		return -EINVAL;

	/* We don't do this yet */
	if (flags & BARRIER_ATTR_MULTISTEP)
		return -EINVAL;

	P_BARRIER("barrier_register %s, nodes = %d, flags =%x\n", name, nodes, flags);
	pthread_mutex_lock(&barrier_list_lock);

	/* See if it already exists */
	if ((barrier = find_barrier(name))) {
		pthread_mutex_unlock(&barrier_list_lock);
		if (nodes != barrier->expected_nodes) {
			log_msg(LOG_ERR, "Barrier registration failed for '%s', expected nodes=%d, requested=%d\n",
			       name, barrier->expected_nodes, nodes);
			return -EINVAL;
		}
		else {
			/* Fill this is as it may have been remote registered */
			barrier->con = con;
			return 0;
		}
	}

	/* Build a new struct and add it to the list */
	barrier = malloc(sizeof (struct cl_barrier));
	if (barrier == NULL) {
		pthread_mutex_unlock(&barrier_list_lock);
		return -ENOMEM;
	}
	memset(barrier, 0, sizeof (*barrier));

	strcpy(barrier->name, name);
	barrier->flags = flags;
	barrier->expected_nodes = nodes;
	barrier->got_nodes = 0;
	barrier->completed_nodes = 0;
	barrier->endreason = 0;
	barrier->registered_nodes = 1;
	barrier->con = con;
	pthread_mutex_init(&barrier->phase2_lock, NULL);
	barrier->state = BARRIER_STATE_INACTIVE;
	pthread_mutex_init(&barrier->lock, NULL);

	list_add(&barrier_list, &barrier->list);
	pthread_mutex_unlock(&barrier_list_lock);

	return 0;
}

static int barrier_setattr_enabled(struct cl_barrier *barrier,
				   unsigned int attr, unsigned long arg)
{
	int status;

	/* Can't disable a barrier */
	if (!arg) {
		pthread_mutex_unlock(&barrier->lock);
		return -EINVAL;
	}

	/* We need to send WAIT now because the user may not
	 * actually call barrier_wait() */
	if (!barrier->waitsent) {
		struct cl_barriermsg bmsg;

		/* Send it to the rest of the cluster */
		bmsg.cmd = CLUSTER_CMD_BARRIER;
		bmsg.subcmd = BARRIER_WAIT;
		strcpy(bmsg.name, barrier->name);

		barrier->waitsent = 1;
		barrier->phase = 1;

		barrier->got_nodes++;

		/* Start the timer if one was wanted */
		if (barrier->timeout) {
			barrier->timer.callback = barrier_timer_fn;
			barrier->timer.arg =  barrier;
			add_timer(&barrier->timer, barrier->timeout, 0);
		}

		/* Barrier WAIT and COMPLETE messages are
		 * always queued - that way they always get
		 * sent out in the right order. If we don't do
		 * this then one can get sent out in the
		 * context of the user process and the other in
		 * cnxman and COMPLETE may /just/ slide in
		 * before WAIT if its in the queue
		 */
		P_BARRIER("Sending WAIT for %s\n", barrier->name);
		status = queue_message(NULL, &bmsg, sizeof (bmsg), NULL, 0, 0);
		if (status < 0) {
			pthread_mutex_unlock(&barrier->lock);
			return status;
		}

		/* It might have been reached now */
		if (barrier
		    && barrier->state != BARRIER_STATE_COMPLETE
		    && barrier->phase == 1)
			check_barrier_complete_phase1(barrier);
	}
	if (barrier && barrier->state == BARRIER_STATE_COMPLETE) {
		pthread_mutex_unlock(&barrier->lock);
		return barrier->endreason;
	}
	pthread_mutex_unlock(&barrier->lock);
	return 0;	/* Nothing to propogate */
}

static int barrier_setattr(char *name, unsigned int attr, unsigned long arg)
{
	struct cl_barrier *barrier;

	/* See if it already exists */
	pthread_mutex_lock(&barrier_list_lock);
	if (!(barrier = find_barrier(name))) {
		pthread_mutex_unlock(&barrier_list_lock);
		return -ENOENT;
	}
	pthread_mutex_unlock(&barrier_list_lock);

	pthread_mutex_lock(&barrier->lock);
	if (barrier->state == BARRIER_STATE_COMPLETE) {
		pthread_mutex_unlock(&barrier->lock);
		return 0;
	}

	switch (attr) {
	case BARRIER_SETATTR_AUTODELETE:
		if (arg)
			barrier->flags |= BARRIER_ATTR_AUTODELETE;
		else
			barrier->flags &= ~BARRIER_ATTR_AUTODELETE;
		pthread_mutex_unlock(&barrier->lock);
		return 0;
		break;

	case BARRIER_SETATTR_TIMEOUT:
		/* Can only change the timout of an inactive barrier */
		if (barrier->state == BARRIER_STATE_WAITING
		    || barrier->waitsent) {
			pthread_mutex_unlock(&barrier->lock);
			return -EINVAL;
		}
		barrier->timeout = arg;
		pthread_mutex_unlock(&barrier->lock);
		return 0;

	case BARRIER_SETATTR_MULTISTEP:
		pthread_mutex_unlock(&barrier->lock);
		return -EINVAL;

	case BARRIER_SETATTR_ENABLED:
		return barrier_setattr_enabled(barrier, attr, arg);

	case BARRIER_SETATTR_NODES:
		/* Can only change the expected node count of an inactive
		 * barrier */
		if (barrier->state == BARRIER_STATE_WAITING
		    || barrier->waitsent)
			return -EINVAL;
		barrier->expected_nodes = arg;
		break;
	}

	pthread_mutex_unlock(&barrier->lock);
	return 0;
}

static int barrier_delete(char *name)
{
	struct cl_barrier *barrier;

	pthread_mutex_lock(&barrier_list_lock);

	/* See if it exists */
	if (!(barrier = find_barrier(name))) {
		pthread_mutex_unlock(&barrier_list_lock);
		return -ENOENT;
	}

	/* Delete it */
	list_del(&barrier->list);
	free(barrier);
	pthread_mutex_unlock(&barrier_list_lock);
	return 0;
}

static int barrier_wait(char *name)
{
	struct cl_barrier *barrier;

	if (!cnxman_running)
		return -ENOTCONN;

	/* Enable it */
	barrier_setattr(name, BARRIER_SETATTR_ENABLED, 1L);

	pthread_mutex_lock(&barrier_list_lock);

	/* See if it still exists - enable may have deleted it! */
	if (!(barrier = find_barrier(name))) {
		pthread_mutex_unlock(&barrier_list_lock);
		return -ENOENT;
	}

	pthread_mutex_lock(&barrier->lock);

	pthread_mutex_unlock(&barrier_list_lock);

	/* If it has already completed then return the status */
	if (barrier->state == BARRIER_STATE_COMPLETE) {
		pthread_mutex_unlock(&barrier->lock);
		send_barrier_complete_msg(barrier);
	}
	else {
		barrier->state = BARRIER_STATE_WAITING;
	}
	pthread_mutex_unlock(&barrier->lock);

	/* User will wait */
	return -EWOULDBLOCK;
}

/* This is called from membership services when a node has left the cluster -
 * we signal all waiting barriers with ESRCH so they know to do something
 * else, if the number of nodes is left at 0 then we compare the new number of
 * nodes in the cluster with that at the barrier and return 0 (success) in that
 * case */
void check_barrier_returns()
{
	struct list *blist;
	struct cl_barrier *barrier;
	int status = 0;

	pthread_mutex_lock(&barrier_list_lock);
	list_iterate(blist, &barrier_list) {
		barrier = list_item(blist, struct cl_barrier);

		if (barrier->waitsent) {
			int wakeit = 0;

			/* Check for a dynamic member barrier */
			if (barrier->expected_nodes == 0) {
				if (barrier->registered_nodes ==
				    cluster_members) {
					status = 0;
					wakeit = 1;
				}
			}
			else {
				status = ESRCH;
				wakeit = 1;
			}

			/* Do we need to tell the barrier? */
			if (wakeit) {
				if (barrier->state == BARRIER_STATE_WAITING) {
					barrier->endreason = status;
					send_barrier_complete_msg(barrier);
				}
			}
		}
	}
	pthread_mutex_unlock(&barrier_list_lock);
}

/* Remote command */
int do_cmd_barrier(struct connection *con, char *cmdbuf, int *retlen)
{
	struct cl_barrier_info info;

	if (!we_are_a_cluster_member)
		return -ENOENT;

	memcpy(&info, cmdbuf, sizeof(info));

	switch (info.cmd) {
	case BARRIER_CMD_REGISTER:
		return barrier_register(con,
					info.name,
					info.flags,
					info.arg);
	case BARRIER_CMD_CHANGE:
		return barrier_setattr(info.name,
				       info.flags,
				       info.arg);
	case BARRIER_CMD_WAIT:
		return barrier_wait(info.name);
	case BARRIER_CMD_DELETE:
		return barrier_delete(info.name);
	default:
		return -EINVAL;
	}
}


void barrier_init()
{
	list_init(&barrier_list);
	pthread_mutex_init(&barrier_list_lock, NULL);
}
