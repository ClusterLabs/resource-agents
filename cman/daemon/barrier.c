#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/errno.h>

#include <openais/service/swab.h>
#include <openais/totem/totemip.h>
#include <openais/totem/aispoll.h>
#include <openais/service/timer.h>
#include "list.h"
#include "cnxman-socket.h"
#include "cnxman-private.h"
#include "daemon.h"
#include "commands.h"
#include "logging.h"
#include "barrier.h"
#include "ais.h"

extern int we_are_a_cluster_member;

LOGSYS_DECLARE_SUBSYS (CMAN_NAME, LOG_INFO);

/* A barrier */
struct cl_barrier {
	struct list list;

	char name[MAX_BARRIER_NAME_LEN];
	unsigned int flags;
	enum { BARRIER_STATE_WAITING, BARRIER_STATE_INACTIVE,
	       BARRIER_STATE_COMPLETE } state;
	unsigned int expected_nodes;
	unsigned int got_nodes;
	unsigned int waitsent;
	unsigned int phase;	/* Completion phase */
	unsigned int endreason;	/* Reason we were woken, usually 0 */
	unsigned int client_complete;
	unsigned long timeout;	/* In seconds */

	struct connection *con;
	openais_timer_handle timer;
};

/* A list of all current barriers */
static struct list barrier_list;

static void send_barrier_complete_msg(struct cl_barrier *barrier)
{
	if (barrier->timeout) {
		openais_timer_delete(barrier->timer);
		barrier->timeout = 0;
	}

	if (!barrier->client_complete) {
		if (barrier->con)
			send_status_return(barrier->con, CMAN_CMD_BARRIER, barrier->endreason);
		barrier->client_complete = 1;
	}
}

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

		barrier->phase = 2;	/* Wait for complete phase II */

		bmsg.cmd = CLUSTER_MSG_BARRIER;
		bmsg.subcmd = BARRIER_COMPLETE;
		strcpy(bmsg.name, barrier->name);

		P_BARRIER("Sending COMPLETE for %s\n", barrier->name);
		comms_send_message((char *) &bmsg, sizeof (bmsg),
				   0, 0,
				   0,
				   MSG_TOTEM_SAFE);
	}
}

/* Do the stuff we need to do when the barrier has been reached */
/* Return 1 if we deleted the barrier */
static int barrier_complete_phase2(struct cl_barrier *barrier, int status)
{
	P_BARRIER("complete_phase2 for %s\n", barrier->name);

	barrier->endreason = status;

	/* Wake up listener */
	if (barrier->state == BARRIER_STATE_WAITING) {
		send_barrier_complete_msg(barrier);
	}
	barrier->state = BARRIER_STATE_COMPLETE;

	/* Delete barrier if autodelete */
	if (barrier->flags & BARRIER_ATTR_AUTODELETE) {
		list_del(&barrier->list);
		free(barrier);
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
	barrier_complete_phase2(barrier, -ETIMEDOUT);
}

static struct cl_barrier *alloc_barrier(char *name, int nodes)
{
	struct cl_barrier *barrier;

	/* Build a new struct and add it to the list */
	barrier = malloc(sizeof (struct cl_barrier));
	if (barrier == NULL) {
		return NULL;
	}
	memset(barrier, 0, sizeof (*barrier));

	strcpy(barrier->name, name);
	barrier->flags = 0;
	barrier->expected_nodes = nodes;
	barrier->got_nodes = 0;
	barrier->endreason = 0;
	barrier->state = BARRIER_STATE_INACTIVE;

	list_add(&barrier_list, &barrier->list);
	return barrier;
}

/* Process BARRIER messages from other nodes */
void process_barrier_msg(struct cl_barriermsg *msg,
			 struct cluster_node *node)
{
	struct cl_barrier *barrier;

	barrier = find_barrier(msg->name);

	/* Ignore other peoples' messages */
	if (!we_are_a_cluster_member)
		return;
	if (!barrier)
		return;

	P_BARRIER("Got %d for %s, from node %s\n", msg->subcmd, msg->name,
		  node ? node->name : "unknown");

	switch (msg->subcmd) {
	case BARRIER_WAIT:
		if (barrier->phase == 0)
			barrier->phase = 1;

		if (barrier->phase == 1) {
			barrier->got_nodes++;
			check_barrier_complete_phase1(barrier);
		}
		break;

	case BARRIER_COMPLETE:
		if (!barrier)
			return;
		/* Once we receive COMPLETE, we know that everyone has completed.
		   I love VS */
		barrier_complete_phase2(barrier, 0);
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

	/* See if it already exists */
	if ((barrier = find_barrier(name))) {
		if (nodes != barrier->expected_nodes) {
			log_printf(LOG_ERR, "Barrier registration failed for '%s', expected nodes=%d, requested=%d\n",
			       name, barrier->expected_nodes, nodes);
			return -EINVAL;
		}
		else {
			/* Fill this is as it may have been remote registered */
			barrier->con = con;
			return 0;
		}
	}

	barrier = alloc_barrier(name, nodes);
	if (!barrier)
		return -ENOMEM;

	barrier->flags = flags;
	barrier->con = con;
	return 0;
}

static int barrier_setattr_enabled(struct cl_barrier *barrier,
				   unsigned int attr, unsigned long arg)
{
	int status;

	/* Can't disable a barrier */
	if (!arg) {
		return -EINVAL;
	}

	/* We need to send WAIT now because the user may not
	 * actually call barrier_wait() */
	if (!barrier->waitsent) {
		struct cl_barriermsg bmsg;

		/* Send it to the rest of the cluster */
		bmsg.cmd = CLUSTER_MSG_BARRIER;
		bmsg.subcmd = BARRIER_WAIT;
		strcpy(bmsg.name, barrier->name);

		barrier->waitsent = 1;
		barrier->phase = 1;

		/* Start the timer if one was wanted */
		if (barrier->timeout) {
			openais_timer_add_duration((unsigned long long)barrier->timeout*1000000000ULL, barrier,
						   barrier_timer_fn, &barrier->timer);
		}

		P_BARRIER("Sending WAIT for %s\n", barrier->name);
		status = comms_send_message((char *)&bmsg, sizeof(bmsg), 0,0, 0, MSG_TOTEM_SAFE);
		if (status < 0) {
			return status;
		}
	}
	if (barrier && barrier->state == BARRIER_STATE_COMPLETE) {
		return barrier->endreason;
	}
	return 0;	/* Nothing to propogate */
}

static int barrier_setattr(char *name, unsigned int attr, unsigned long arg)
{
	struct cl_barrier *barrier;

	/* See if it already exists */
	if (!(barrier = find_barrier(name))) {
		return -ENOENT;
	}

	if (barrier->state == BARRIER_STATE_COMPLETE) {
		return 0;
	}

	switch (attr) {
	case BARRIER_SETATTR_AUTODELETE:
		if (arg)
			barrier->flags |= BARRIER_ATTR_AUTODELETE;
		else
			barrier->flags &= ~BARRIER_ATTR_AUTODELETE;
		return 0;
		break;

	case BARRIER_SETATTR_TIMEOUT:
		/* Can only change the timout of an inactive barrier */
		if (barrier->state == BARRIER_STATE_WAITING
		    || barrier->waitsent) {
			return -EINVAL;
		}
		barrier->timeout = arg;
		return 0;

	case BARRIER_SETATTR_MULTISTEP:
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

	return 0;
}

static int barrier_delete(char *name)
{
	struct cl_barrier *barrier;

	/* See if it exists */
	if (!(barrier = find_barrier(name))) {
		return -ENOENT;
	}

	/* Delete it */
	list_del(&barrier->list);
	free(barrier);
	return 0;
}

static int barrier_wait(char *name)
{
	struct cl_barrier *barrier;

	/* Enable it */
	barrier_setattr(name, BARRIER_SETATTR_ENABLED, 1L);

	/* See if it still exists - enable may have deleted it! */
	if (!(barrier = find_barrier(name))) {
		return -ENOENT;
	}

	/* If it has already completed then return the status */
	if (barrier->state == BARRIER_STATE_COMPLETE) {
		send_barrier_complete_msg(barrier);
	}
	else {
		barrier->state = BARRIER_STATE_WAITING;
	}

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

	list_iterate(blist, &barrier_list) {
		barrier = list_item(blist, struct cl_barrier);

		if (barrier->waitsent) {
			int wakeit = 0;

			/* Check for a dynamic member barrier */
			if (barrier->expected_nodes == 0) {
				status = 0;
				wakeit = 1;
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

/* Remove any barriers associated with this connection */
void remove_barriers(struct connection *con)
{
	struct list *blist, *tmp;
	struct cl_barrier *bar;

	list_iterate_safe(blist, tmp, &barrier_list) {
		bar = list_item(blist, struct cl_barrier);

		if (con == bar->con) {
			list_del(&bar->list);
			free(bar);
		}
	}
}

void barrier_init()
{
	list_init(&barrier_list);
}
