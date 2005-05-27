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

int do_done(char *name, int level, int event_nr)
{
	group_t *g;

	g = find_group_level(name, level);
	if (!g)
		return -ENOENT;

	log_group(g, "got done event_nr %d", event_nr);

	if (g->state == GST_RECOVER) {
		if (!check_recovery(g, event_nr)) {
			log_error(g, "do_done recover inval nr %d", event_nr);
			return -EINVAL;
		}

		log_group(g, "recover state %u", g->recover_state);

		if (g->recover_state == RECOVER_START)
			g->recover_state = RECOVER_STARTDONE;
		else
			log_error(g, "do_done recover state %u",
				  g->recover_state);
	}

	/* We need to check for in_update() before in_event() because
	   there may be both, in which case the callback should always
	   be for the update. */

	else if (in_update(g)) {
		update_t *up = g->update;

		if (!up) {
			log_error(g, "do_done event_nr %d no update", event_nr);
			return -EINVAL;
		}

		if (up->id != event_nr) {
			log_error(g, "do_done update %d invalid %d %d",
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
		} else
			log_error(g, "do_done ignored %d in_update", event_nr);
	}

	else if (in_event(g)) {
		event_t *ev = g->event;

		if (!ev) {
			log_error(g, "do_done event_nr %d no event", event_nr);
			return -EINVAL;
		}

		if (ev->id != event_nr) {
			log_error(g, "do_done event %d invalid %d %d",
				  event_nr, ev->id, event_id);
			ASSERT(0,);
			return -EINVAL;
		}

		if (test_bit(EFL_ALLOW_STARTDONE, &ev->flags)) {
			clear_bit(EFL_ALLOW_STARTDONE, &ev->flags);
			if (ev->state == EST_JSTART_SERVICEWAIT)
				ev->state = EST_JSTART_SERVICEDONE;
		} else
			log_error(g, "do_done ignored %d in_event", event_nr);

	}

	else
		log_error(g, "do_done ignored %u state %d", event_nr, g->state);
}

