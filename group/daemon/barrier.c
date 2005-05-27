/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "gd_internal.h"

struct barrier_wait {
	struct list_head list;
	group_t *group;
	char name[MAX_BARRIERLEN+1];
	int acks;
	int acks_needed;
	int type;
};

extern struct list_head gd_barriers;


static void complete_startdone_barrier_new(group_t *g, int status)
{
	event_t *ev = g->event;

	if (!ev) {
		log_group(g, "complete_startdone_barrier_new no event");
		return;
	}

	if (!test_bit(EFL_ALLOW_BARRIER, &ev->flags)) {
		log_group(g, "complete_startdone_barrier_new not allowed");
		return;
	}
	clear_bit(EFL_ALLOW_BARRIER, &ev->flags);

	ev->barrier_status = status;
	ev->state = EST_BARRIER_DONE;
}

static void complete_startdone_barrier(group_t *g, int status)
{
	update_t *up = g->update;

	if (!up) {
		log_group(g, "complete_startdone_barrier no update");
		return;
	}

	if (!test_bit(UFL_ALLOW_BARRIER, &up->flags)) {
		log_group(g, "complete_startdone_barrier not allowed");
		return;
	}
	clear_bit(UFL_ALLOW_BARRIER, &up->flags);

	up->barrier_status = status;
	up->state = UST_BARRIER_DONE;
}

static void complete_recovery_barrier(group_t *g, int status)
{
	if (status) {
		log_error(g, "complete_recovery_barrier status=%d", status);
		return;
	}

	if (g->state != GST_RECOVER || g->recover_state != RECOVER_BARRIERWAIT){
		log_error(g, "complete_recovery_barrier state %d recover %d",
			  g->state, g->recover_state);
		return;
	}

	if (!g->recover_stop)
		g->recover_state = RECOVER_STOP;
	else
		g->recover_state = RECOVER_BARRIERDONE;
}

int process_barriers(void)
{
	struct barrier_wait *bw, *safe;
	int rv = 0;

	list_for_each_entry_safe(bw, safe, &gd_barriers, list) {

		/*
		log_print("process_barrier %d/%d: %s",
			  bw->acks, bw->acks_needed, bw->name);
		*/

		if (bw->acks == bw->acks_needed) {
			if (!bw->group) {
				log_print("barrier with no group: %s",
					  bw->name);
				continue;
			}

			list_del(&bw->list);

			switch (bw->type) {
			case GD_BARRIER_STARTDONE:
				complete_startdone_barrier(bw->group, 0);
				break;
			case GD_BARRIER_STARTDONE_NEW:
				complete_startdone_barrier_new(bw->group, 0);
				break;
			case GD_BARRIER_RECOVERY:
				complete_recovery_barrier(bw->group, 0);
				break;
			}

			free(bw);
			rv++;
		}
	}

	return rv;
}

static struct barrier_wait *find_barrier(char *name)
{
	struct barrier_wait *bw;

	list_for_each_entry(bw, &gd_barriers, list) {
		if ((strlen(bw->name) == strlen(name)) &&
		    !strncmp(bw->name, name, strlen(name)))
			return bw;
	}
	return NULL;
}

static struct barrier_wait *create_barrier(char *name)
{
	struct barrier_wait *bw;

	bw = malloc(sizeof(struct barrier_wait));
	if (!bw)
		return NULL;
	memset(bw, 0, sizeof(*bw));
	strncpy(bw->name, name, MAX_BARRIERLEN);
	return bw;
}

void process_barrier_msg(msg_t *msg, int nodeid)
{
	struct barrier_wait *bw;
	char name[MAX_BARRIERLEN+1];

	if (nodeid == gd_nodeid)
		return;

	memset(name, 0, sizeof(name));
	memcpy(name, msg->ms_info, MAX_BARRIERLEN);

	bw = find_barrier(name);
	if (bw)
		bw->acks++;
	else {
		bw = create_barrier(name);
		if (!bw)
			return;
		bw->acks = 1;
		list_add(&bw->list, &gd_barriers);
	}

	log_print("barrier from %d %d/%d: %s", nodeid,
		  bw->acks, bw->acks_needed, name);
}

int do_barrier(group_t *g, char *name, int count, int type)
{
	struct barrier_wait *bw;
	char *mbuf;
	msg_t *msg;
	int len;

	bw = find_barrier(name);
	if (!bw) {
		bw = create_barrier(name);
		if (!bw)
			return -1;
		list_add(&bw->list, &gd_barriers);
	}

	bw->group = g;
	bw->type = type;
	bw->acks_needed = count;
	bw->acks++;

	log_group(g, "do_barrier %d/%d: %s", bw->acks, bw->acks_needed,
		  bw->name);

	mbuf = create_msg(g, SMSG_BARRIER, 0, &len, NULL);
	msg = (msg_t *) mbuf;
	strncpy(msg->ms_info, name, MAX_BARRIERLEN);

	send_members_message(g, mbuf, len);

	return 0;
}

void cancel_recover_barrier(group_t *g)
{
	struct barrier_wait *bw;

	bw = find_barrier(g->recover_barrier);
	if (bw) {
		log_group(g, "cancel_recover_barrier %s", g->recover_barrier);
		list_del(&bw->list);
		free(bw);
	}
}

void cancel_update_barrier(group_t *g)
{
	struct barrier_wait *bw;
	update_t *up = g->update;
	char name[MAX_BARRIERLEN+1];

	clear_bit(UFL_ALLOW_BARRIER, &up->flags);

	memset(name, 0, sizeof(name));
	snprintf(name, MAX_BARRIERLEN, "sm.%u.%u.%u.%u",
		 g->global_id, up->nodeid, up->remote_seid, g->memb_count);

	bw = find_barrier(name);
	if (bw) {
		log_group(g, "cancel_update_barrier %s", name);
		list_del(&bw->list);
		free(bw);
	}
}

