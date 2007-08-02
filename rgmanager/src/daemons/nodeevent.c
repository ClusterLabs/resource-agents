/*
  Copyright Red Hat, Inc. 2006

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2, or (at your option) any
  later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to the
  Free Software Foundation, Inc.,  675 Mass Ave, Cambridge, 
  MA 02139, USA.
*/
#include <resgroup.h>
#include <rg_locks.h>
#include <gettid.h>
#include <assert.h>
#include <libcman.h>
#include <ccs.h>
#include <clulog.h>

typedef struct __ne_q {
	list_head();
	int ne_local;
	int ne_nodeid;
	int ne_state;
	int ne_clean;
} nevent_t;

/**
 * Node event queue.
 */
#ifdef WRAP_LOCKS
static pthread_mutex_t ne_queue_mutex = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;
#else
static pthread_mutex_t ne_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif
static nevent_t *event_queue = NULL;
static pthread_t ne_thread = 0;
static int transition_throttling = 5;
int ne_queue_request(int local, int nodeid, int state);

void hard_exit(void);
int init_resource_groups(int);
void flag_shutdown(int sig);
void flag_reconfigure(int sig);

extern int running;
extern int shutdown_pending;


void
set_transition_throttling(int nsecs)
{
	if (nsecs < 0)
		nsecs = 0;
	transition_throttling = nsecs;
}


/**
  Called to handle the transition of a cluster member from up->down or
  down->up.  This handles initializing services (in the local node-up case),
  exiting due to loss of quorum (local node-down), and service fail-over
  (remote node down).
 
  @param nodeID		ID of the member which has come up/gone down.
  @param nodeStatus		New state of the member in question.
  @see eval_groups
 */
void
node_event(int local, int nodeID, int nodeStatus, int clean)
{
	if (!running)
		return;

	if (local) {

		/* Local Node Event */
		if (nodeStatus == 0) {
			clulog(LOG_ERR, "Exiting uncleanly\n");
			hard_exit();
		}

		if (!rg_initialized()) {
			if (init_resource_groups(0) != 0) {
				clulog(LOG_ERR,
				       "#36: Cannot initialize services\n");
				hard_exit();
			}
		}

		if (shutdown_pending) {
			clulog(LOG_NOTICE, "Processing delayed exit signal\n");
			running = 0;
			return;
		}
		setup_signal(SIGINT, flag_shutdown);
		setup_signal(SIGTERM, flag_shutdown);
		setup_signal(SIGHUP, flag_reconfigure);

		/* Let things settle if we're booting multiple */
		if (transition_throttling)
			sleep(transition_throttling);

		eval_groups(1, nodeID, 1);
		return;
	}

	/*
	 * Nothing to do for events from other nodes if we are not ready.
	 */
	if (!rg_initialized()) {
		clulog(LOG_DEBUG, "Services not initialized.\n");
		return;
	}

	eval_groups(0, nodeID, nodeStatus);
}


int
node_has_fencing(int nodeid)
{
	int ccs_desc;
	char *val = NULL;
	char buf[1024];
	int ret = 1;
	
	ccs_desc = ccs_connect();
	if (ccs_desc < 0) {
		clulog(LOG_ERR, "Unable to connect to ccsd; cannot handle"
		       " node event!\n");
		/* Assume node has fencing */
		return 1;
	}

	snprintf(buf, sizeof(buf), 
		 "/cluster/clusternodes/clusternode[@nodeid=\"%d\"]"
		 "/fence/method/device/@name", nodeid);

	if (ccs_get(ccs_desc, buf, &val) != 0)
		ret = 0;
	if (val) 
		free(val);
	ccs_disconnect(ccs_desc);
	return ret;
}


int
node_fenced(int nodeid)
{
	cman_handle_t ch;
	int fenced = 0;
	uint64_t fence_time;

	ch = cman_init(NULL);
	if (cman_get_fenceinfo(ch, nodeid, &fence_time, &fenced, NULL) < 0)
		fenced = 0;

	cman_finish(ch);

	return fenced;
}


void *
node_event_thread(void *arg)
{
	nevent_t *ev;
	int notice;

	while (1) {
		pthread_mutex_lock(&ne_queue_mutex);
		ev = event_queue;
		if (ev)
			list_remove(&event_queue, ev);
		else
			break; /* We're outta here */
		pthread_mutex_unlock(&ne_queue_mutex);

		if (ev->ne_state == 0 && !ev->ne_clean &&
		    node_has_fencing(ev->ne_nodeid)) {
			notice = 0;
			while (!node_fenced(ev->ne_nodeid)) {
				if (!notice) {
					notice = 1;
					clulog(LOG_INFO, "Waiting for "
					       "node #%d to be fenced\n",
					       ev->ne_nodeid);
				}
				sleep(2);
			}
			if (notice)
				clulog(LOG_INFO, "Node #%d fenced; "
				       "continuing\n", ev->ne_nodeid);
		}

		node_event(ev->ne_local, ev->ne_nodeid, ev->ne_state,
			   ev->ne_clean);

		free(ev);
	}

	/* Mutex held */
	ne_thread = 0;
	pthread_mutex_unlock(&ne_queue_mutex);
	pthread_exit(NULL);
}


void
node_event_q(int local, int nodeID, int state, int clean)
{
	nevent_t *ev;
	pthread_attr_t attrs;

	while (1) {
		ev = malloc(sizeof(nevent_t));
		if (ev) {
			break;
		}
		sleep(1);
	}

	memset(ev,0,sizeof(*ev));

	ev->ne_state = state;
	ev->ne_local = local;
	ev->ne_nodeid = nodeID;
	ev->ne_clean = clean;

	pthread_mutex_lock (&ne_queue_mutex);
	list_insert(&event_queue, ev);
	if (ne_thread == 0) {
        	pthread_attr_init(&attrs);
        	pthread_attr_setinheritsched(&attrs, PTHREAD_INHERIT_SCHED);
        	pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED);
		pthread_attr_setstacksize(&attrs, 262144);

		pthread_create(&ne_thread, &attrs, node_event_thread, NULL);
        	pthread_attr_destroy(&attrs);
	}
	pthread_mutex_unlock (&ne_queue_mutex);
}
