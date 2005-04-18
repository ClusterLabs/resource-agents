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

#include "dlm_internal.h"
#include "member.h"
#include "lock.h"

struct rq_entry {
	struct list_head list;
	int nodeid;
	char request[1];
};

/*
 * Requests received while the lockspace is in recovery get added to the
 * request queue and processed when recovery is complete.  This happens when
 * the lockspace is suspended on some nodes before it is on others, or the
 * lockspace is enabled on some while still suspended on others.
 */

void dlm_add_requestqueue(struct dlm_ls *ls, int nodeid, struct dlm_header *hd)
{
	struct dlm_message *ms = (struct dlm_message *) hd;
	struct rq_entry *e;
	int length = hd->h_length;

	if (dlm_is_removed(ls, nodeid))
		return;

	e = kmalloc(sizeof(struct rq_entry) + length, GFP_KERNEL);
	if (!e) {
		printk("dlm_add_requestqueue: out of memory\n");
		return;
	}

	log_debug(ls, "add_requestqueue type 0x%x from %d", ms->m_type, nodeid);

	e->nodeid = nodeid;
	memcpy(e->request, hd, length);

	down(&ls->ls_requestqueue_lock);
	list_add_tail(&e->list, &ls->ls_requestqueue);
	up(&ls->ls_requestqueue_lock);
}

int dlm_process_requestqueue(struct dlm_ls *ls)
{
	struct rq_entry *e;
	struct dlm_header *hd;
	struct dlm_message *ms;
	int error = 0;

	log_debug(ls, "process_requestqueue");

	down(&ls->ls_requestqueue_lock);

	for (;;) {
		if (list_empty(&ls->ls_requestqueue)) {
			up(&ls->ls_requestqueue_lock);
			error = 0;
			break;
		}
		e = list_entry(ls->ls_requestqueue.next, struct rq_entry, list);
		up(&ls->ls_requestqueue_lock);

		hd = (struct dlm_header *) e->request;
		ms = (struct dlm_message *) hd;

		log_debug(ls, "process_requestqueue type 0x%x from %u",
			  ms->m_type, e->nodeid);

		error = dlm_receive_message(hd, e->nodeid, TRUE);

		if (error == -EINTR) {
			/* entry is left on requestqueue */
			log_debug(ls, "process_requestqueue abort eintr");
			break;
		}

		down(&ls->ls_requestqueue_lock);
		list_del(&e->list);
		kfree(e);

		if (!test_bit(LSFL_LS_RUN, &ls->ls_flags)) {
			log_debug(ls, "process_requestqueue abort ls_run");
			up(&ls->ls_requestqueue_lock);
			error = -EINTR;
			break;
		}
		schedule();
	}

	return error;
}

/*
 * After recovery is done, locking is resumed and dlm_recoverd takes all the
 * saved requests and processes them as they would have been by dlm_recvd.  At
 * the same time, dlm_recvd will start receiving new requests from remote
 * nodes.  We want to delay dlm_recvd processing new requests until
 * dlm_recoverd has finished processing the old saved requests.
 */

void dlm_wait_requestqueue(struct dlm_ls *ls)
{
	for (;;) {
		down(&ls->ls_requestqueue_lock);
		if (list_empty(&ls->ls_requestqueue))
			break;
		if (!test_bit(LSFL_LS_RUN, &ls->ls_flags))
			break;
		up(&ls->ls_requestqueue_lock);
		schedule();
	}
	up(&ls->ls_requestqueue_lock);
}

/*
 * Dir lookups and lookup replies send before recovery are invalid because the
 * directory is rebuilt during recovery, so don't save any requests of this
 * type.  Don't save any requests from a node that's being removed either.
 */

void dlm_purge_requestqueue(struct dlm_ls *ls)
{
	struct dlm_message *ms;
	struct rq_entry *e, *safe;
	uint32_t mstype;

	log_debug(ls, "purge_requestqueue");

	down(&ls->ls_requestqueue_lock);
	list_for_each_entry_safe(e, safe, &ls->ls_requestqueue, list) {

		ms = (struct dlm_message *) e->request;
		mstype = le32_to_cpu(ms->m_type);

		if (dlm_is_removed(ls, e->nodeid) ||
		    mstype == DLM_MSG_REMOVE ||
	            mstype == DLM_MSG_LOOKUP ||
	            mstype == DLM_MSG_LOOKUP_REPLY) {
			list_del(&e->list);
			kfree(e);
		}
	}
	up(&ls->ls_requestqueue_lock);
}

