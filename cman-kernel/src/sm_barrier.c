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

#include "sm.h"

static struct list_head	barriers;
static spinlock_t	barriers_lock;

struct bc_entry {
	struct list_head list;
	uint32_t gid;
	int status;
	char type;
};
typedef struct bc_entry bc_entry_t;

void init_barriers(void)
{
	INIT_LIST_HEAD(&barriers);
	spin_lock_init(&barriers_lock);
}

static int atoi(char *c)
{
	int x = 0;

	while ('0' <= *c && *c <= '9') {
		x = x * 10 + (*c - '0');
		c++;
	}
	return x;
}

static void add_barrier_callback(char *name, int status, int type)
{
	char *p;
	uint32_t gid;
	bc_entry_t *be;

	/* an ESRCH callback just means there was a cnxman transition */
	if (status == -ESRCH)
		return;

	/* extract global id of SG from barrier name */
	p = strstr(name, "sm.");

	SM_ASSERT(p, printk("name=\"%s\" status=%d\n", name, status););

	p += strlen("sm.");
	gid = atoi(p);

	SM_RETRY(be = kmalloc(sizeof(bc_entry_t), GFP_ATOMIC), be);

	be->gid = gid;
	be->status = status;
	be->type = type;

	spin_lock(&barriers_lock);
	list_add_tail(&be->list, &barriers);
	spin_unlock(&barriers_lock);

	wake_serviced(DO_BARRIERS);
}

static void callback_recovery_barrier(char *name, int status)
{
	add_barrier_callback(name, status, SM_BARRIER_RECOVERY);
}

static void callback_startdone_barrier_new(char *name, int status)
{
	add_barrier_callback(name, status, SM_BARRIER_STARTDONE_NEW);
}

static void callback_startdone_barrier(char *name, int status)
{
	add_barrier_callback(name, status, SM_BARRIER_STARTDONE);
}

int sm_barrier(char *name, int count, int type)
{
	int error;
	unsigned long fn = 0;

	switch (type) {
	case SM_BARRIER_STARTDONE:
		fn = (unsigned long) callback_startdone_barrier;
		break;
	case SM_BARRIER_STARTDONE_NEW:
		fn = (unsigned long) callback_startdone_barrier_new;
		break;
	case SM_BARRIER_RECOVERY:
		fn = (unsigned long) callback_recovery_barrier;
		break;
	}

	error = kcl_barrier_register(name, 0, count);
	if (error) {
		log_print("barrier register error %d", error);
		goto fail;
	}

	error = kcl_barrier_setattr(name, BARRIER_SETATTR_AUTODELETE, TRUE);
	if (error) {
		log_print("barrier setattr autodel error %d", error);
		goto fail_bar;
	}

	error = kcl_barrier_setattr(name, BARRIER_SETATTR_CALLBACK, fn);
	if (error) {
		log_print("barrier setattr cb error %d", error);
		goto fail_bar;
	}

	error = kcl_barrier_setattr(name, BARRIER_SETATTR_ENABLED, TRUE);
	if (error) {
		log_print("barrier setattr enabled error %d", error);
		goto fail_bar;
	}

	return 0;

 fail_bar:
	kcl_barrier_delete(name);
 fail:
	return error;
}

void process_startdone_barrier_new(sm_group_t *sg, int status)
{
	sm_sevent_t *sev = sg->sevent;

	if (!test_and_clear_bit(SEFL_ALLOW_BARRIER, &sev->se_flags)) {
		log_debug(sev->se_sg, "ignore barrier cb status %d", status);
		return;
	}

	sev->se_barrier_status = status;
	sev->se_state = SEST_BARRIER_DONE;
	set_bit(SEFL_CHECK, &sev->se_flags);
	wake_serviced(DO_JOINLEAVE);
}

void process_startdone_barrier(sm_group_t *sg, int status)
{
	sm_uevent_t *uev = &sg->uevent;

	if (!test_and_clear_bit(UEFL_ALLOW_BARRIER, &uev->ue_flags)) {
		log_debug(sg, "ignore barrier cb status %d", status);
		return;
	}

	uev->ue_barrier_status = status;
	uev->ue_state = UEST_BARRIER_DONE;
	set_bit(UEFL_CHECK, &uev->ue_flags);
	wake_serviced(DO_MEMBERSHIP);
}

void process_recovery_barrier(sm_group_t *sg, int status)
{
	if (status) {
		log_error(sg, "process_recovery_barrier status=%d", status);
		return;
	}

	if (sg->state != SGST_RECOVER ||
	    sg->recover_state != RECOVER_BARRIERWAIT) {
		log_error(sg, "process_recovery_barrier state %d recover %d",
			  sg->state, sg->recover_state);
		return;
	}

	if (!sg->recover_stop)
		sg->recover_state = RECOVER_STOP;
	else
		sg->recover_state = RECOVER_BARRIERDONE;

	wake_serviced(DO_RECOVERIES);
}

void process_barriers(void)
{
	sm_group_t *sg;
	bc_entry_t *be;

	while (1) {
		be = NULL;

		spin_lock(&barriers_lock);
		if (!list_empty(&barriers)) {
			be = list_entry(barriers.next, bc_entry_t, list);
			list_del(&be->list);
		}
		spin_unlock(&barriers_lock);

		if (!be)
			break;

		sg = sm_global_id_to_sg(be->gid);
		if (!sg) {
			log_print("process_barriers: no sg %08x", be->gid);
			break;
		}

		switch (be->type) {
		case SM_BARRIER_STARTDONE_NEW:
			process_startdone_barrier_new(sg, be->status);
			break;

		case SM_BARRIER_STARTDONE:
			process_startdone_barrier(sg, be->status);
			break;

		case SM_BARRIER_RECOVERY:
			process_recovery_barrier(sg, be->status);
			break;
		}

		kfree(be);
		schedule();
	}
}
