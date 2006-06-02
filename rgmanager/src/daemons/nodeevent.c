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

typedef struct __ne_q {
	list_head();
	int ne_local;
	uint64_t ne_nodeid;
	int ne_state;
} nevent_t;

int node_event(int, uint64_t, int);

/**
 * Node event queue.
 */
static nevent_t *event_queue = NULL;
static pthread_mutex_t ne_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t ne_thread = 0;
int ne_queue_request(int local, uint64_t nodeid, int state);


void *
node_event_thread(void *arg)
{
	nevent_t *ev;

	while (1) {
		pthread_mutex_lock(&ne_queue_mutex);
		ev = event_queue;
		if (ev)
			list_remove(&event_queue, ev);
		else
			break; /* We're outta here */
		pthread_mutex_unlock(&ne_queue_mutex);

		node_event(ev->ne_local, ev->ne_nodeid, ev->ne_state);

		free(ev);
	}

	/* Mutex held */
	ne_thread = 0;
	rg_dec_threads();
	pthread_mutex_unlock(&ne_queue_mutex);
	return NULL;
}


void
node_event_q(int local, uint64_t nodeID, int state)
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

	pthread_mutex_lock (&ne_queue_mutex);
	list_insert(&event_queue, ev);
	if (ne_thread == 0) {
        	pthread_attr_init(&attrs);
        	pthread_attr_setinheritsched(&attrs, PTHREAD_INHERIT_SCHED);
        	pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED);
		pthread_attr_setstacksize(&attrs, 262144);

		pthread_create(&ne_thread, &attrs, node_event_thread, NULL);
        	pthread_attr_destroy(&attrs);

		rg_inc_threads();
	}
	pthread_mutex_unlock (&ne_queue_mutex);
}
