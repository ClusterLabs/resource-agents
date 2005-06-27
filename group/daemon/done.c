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

#include "gd_internal.h"

extern uint32_t event_id;

int do_stopdone(char *name, int level)
{
	group_t *g;

	g = find_group_level(name, level);
	if (!g)
		return -ENOENT;

	log_group(g, "got stopdone");
	set_bit(GFL_STOPPED, &g->flags);

	/* we're recovering a group, recover_state should be in
	   RECOVER_LOCAL_STOPWAIT and we move it to RECOVER_LOCAL_STOPPED */

	if (g->state == GST_RECOVER) {
		log_group(g, "recover state %u", g->recover_state);
		if (g->recover_state == RECOVER_LOCAL_STOPWAIT)
			g->recover_state = RECOVER_LOCAL_STOPPED;
		else
			log_error(g, "do_stopdone recover state %u",
				  g->recover_state);
	}

	/* we're in an update adding a new (joining) node,
	   up->state should be in UST_JSTOP_SERVICEWAIT and we move it to
	   UST_JSTOP_SERVICEDONE  */

	/* we're in an update removing a (leaving) member,
	   up->state should be in UST_LSTOP_SERVICEWAIT and move it to
	   UST_LSTOP_SERVICEDONE */

	else if (in_update(g)) {
		update_t *up = g->update;
		if (!up) {
			log_error(g, "do_stopdone no update");
			return -EINVAL;
		}

		if (up->state == UST_JSTOP_SERVICEWAIT)
			up->state = UST_JSTOP_SERVICEDONE;
		else if (up->state == UST_LSTOP_SERVICEWAIT)
			up->state = UST_LSTOP_SERVICEDONE;
		else
			log_error(g, "do_stopdone update state %u", up->state);
	}

	/* we've leaving the group and have stopped the local app,
	   move ev->state to LSTOP_ACKED if we've received all the needed
	   replies in process_reply() -- if not, then process_reply() will
	   move to LSTOP_ACKED when the final reply is received and it
	   sees that there's been a stopdone */

	else if (in_event(g)) {
		event_t *ev = g->event;
		if (!ev) {
			log_error(g, "do_stopdone no event");
			return -EINVAL;
		}

		if (ev->state != EST_LSTOP_ACKWAIT)
			log_error(g, "do_stopdone event state %u", ev->state);

		if (test_bit(EFL_ACKED, &ev->flags))
			ev->state = EST_LSTOP_ACKED;
	}

	/* we've done a group_stop() as part of error handling, event backout,
	   or update cancelation, needs investigation -- also look at how to
	   avoid ever reverting a node that's being added once groups have
	   been told to add it */

	else
		log_error(g, "do_stopdone ignored state %d", g->state);

	return 0;
}

int do_startdone(char *name, int level, int event_nr)
{
	group_t *g;

	g = find_group_level(name, level);
	if (!g)
		return -ENOENT;

	log_group(g, "got startdone event_nr %d", event_nr);

	if (g->state == GST_RECOVER) {
		if (!check_recovery(g, event_nr)) {
			log_error(g, "do_startdone recover inval nr %d",
				  event_nr);
			return -EINVAL;
		}

		log_group(g, "recover state %u", g->recover_state);

		if (g->recover_state == RECOVER_START)
			g->recover_state = RECOVER_STARTDONE;
		else
			log_error(g, "do_startdone recover state %u",
				  g->recover_state);
	}

	/* We need to check for in_update() before in_event() because
	   there may be both, in which case the callback should always
	   be for the update. */

	else if (in_update(g)) {
		update_t *up = g->update;

		if (!up) {
			log_error(g, "do_startdone event_nr %d no update",
				  event_nr);
			return -EINVAL;
		}

		if (up->id != event_nr) {
			log_error(g, "do_startdone update %d invalid %d %d",
				  event_nr, up->id, event_id);
			ASSERT(0,);
			return -EINVAL;
		}

		if (test_bit(UFL_ALLOW_STARTDONE, &up->flags)) {
			clear_bit(UFL_ALLOW_STARTDONE, &up->flags);
			if (up->state == UST_JSTART_SERVICEWAIT)
				up->state = UST_JSTART_SERVICEDONE;
			else if (up->state == UST_LSTART_SERVICEWAIT)
				up->state = UST_LSTART_SERVICEDONE;
			else
				log_error(g, "do_startdone update state %d",
					  up->state);
		} else
			log_error(g, "do_startdone ignored %d in_update",
				  event_nr);
	}

	else if (in_event(g)) {
		event_t *ev = g->event;

		if (!ev) {
			log_error(g, "do_startdone event_nr %d no event",
				  event_nr);
			return -EINVAL;
		}

		if (ev->id != event_nr) {
			log_error(g, "do_startdone event %d invalid %d %d",
				  event_nr, ev->id, event_id);
			ASSERT(0,);
			return -EINVAL;
		}

		if (test_bit(EFL_ALLOW_STARTDONE, &ev->flags)) {
			clear_bit(EFL_ALLOW_STARTDONE, &ev->flags);
			if (ev->state == EST_JSTART_SERVICEWAIT)
				ev->state = EST_JSTART_SERVICEDONE;
		} else
			log_error(g, "do_startdone ignored %d in_event",
				  event_nr);

	} else
		log_error(g, "do_startdone ignored %u state %d",
			  event_nr, g->state);

	return 0;
}

