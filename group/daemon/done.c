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

int do_done(char *name, int level, int event_nr)
{
	group_t *g;

	g = find_group_level(name, level);
	if (!g)
		return -ENOENT;

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

	else if (in_event(g)) {
		event_t *ev = g->event;

		if (!ev || ev->id != event_nr) {
			log_error(g, "do_done event inval nr %d %d",
				  event_nr, ev ? ev->id : -1);
			return -EINVAL;
		}

		/* if (test_and_clear_bit(EFL_ALLOW_STARTDONE, &ev->flags) */
		if (ev->state == EST_JSTART_SERVICEWAIT)
			ev->state = EST_JSTART_SERVICEDONE;
	}

	else if (in_update(g)) {
		update_t *up = g->update;

		if (!up || up->id != event_nr) {
			log_error(g, "do_done update inval nr %d", event_nr);
			return -EINVAL;
		}

		/* if (test_and_clear_bit(UFL_ALLOW_STARTDONE, &up->flags)) */
		if (up->state == UST_JSTART_SERVICEWAIT)
			up->state = UST_JSTART_SERVICEDONE;
		else if (up->state == UST_LSTART_SERVICEWAIT)
			up->state = UST_LSTART_SERVICEDONE;
	}

	else
		log_error(g, "ignoring done callback event_nr %u", event_nr);
}

