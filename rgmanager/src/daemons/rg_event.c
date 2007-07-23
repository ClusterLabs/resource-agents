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

typedef struct __rge_q {
	list_head();
	char rg_name[128];
	uint32_t rg_state;
	int rg_owner;
} rgevent_t;


/**
 * resource group event queue.
 */
static rgevent_t *rg_ev_queue = NULL;
static pthread_mutex_t rg_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t rg_ev_thread = 0;

void group_event(char *name, uint32_t state, int owner);


void *
rg_event_thread(void *arg)
{
	rgevent_t *ev;

	while (1) {
		pthread_mutex_lock(&rg_queue_mutex);
		ev = rg_ev_queue;
		if (ev)
			list_remove(&rg_ev_queue, ev);
		else
			break; /* We're outta here */
		pthread_mutex_unlock(&rg_queue_mutex);

		group_event(ev->rg_name, ev->rg_state, ev->rg_owner);

		free(ev);
	}

	/* Mutex held */
	rg_ev_thread = 0;
	pthread_mutex_unlock(&rg_queue_mutex);
	pthread_exit(NULL);
}


void
rg_event_q(char *name, uint32_t state, int owner)
{
	rgevent_t *ev;
	pthread_attr_t attrs;

	while (1) {
		ev = malloc(sizeof(rgevent_t));
		if (ev) {
			break;
		}
		sleep(1);
	}

	memset(ev,0,sizeof(*ev));

	strncpy(ev->rg_name, name, 128);
	ev->rg_state = state;
	ev->rg_owner = owner;

	pthread_mutex_lock (&rg_queue_mutex);
	list_insert(&rg_ev_queue, ev);
	if (rg_ev_thread == 0) {
        	pthread_attr_init(&attrs);
        	pthread_attr_setinheritsched(&attrs, PTHREAD_INHERIT_SCHED);
        	pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED);
		pthread_attr_setstacksize(&attrs, 262144);

		pthread_create(&rg_ev_thread, &attrs, rg_event_thread, NULL);
        	pthread_attr_destroy(&attrs);
	}
	pthread_mutex_unlock (&rg_queue_mutex);
}
