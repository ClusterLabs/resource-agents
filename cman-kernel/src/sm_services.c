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

static struct list_head	callbacks;
static spinlock_t	callback_lock;
static struct list_head	sg_registered[SG_LEVELS];

/*
 * These are the functions to register, join, leave, unregister, callback
 * with/to the sm.
 */

struct sc_entry {
	struct list_head list;
	uint32_t local_id;
	int event_id;
};
typedef struct sc_entry sc_entry_t;

void init_services(void)
{
	int i;

	INIT_LIST_HEAD(&callbacks);
	spin_lock_init(&callback_lock);

	for (i = 0; i < SG_LEVELS; i++) {
		INIT_LIST_HEAD(&sm_sg[i]);
		INIT_LIST_HEAD(&sg_registered[i]);
	}
	init_MUTEX(&sm_sglock);
}

/* Context: service */

int kcl_register_service(char *name, int namelen, int level,
			 struct kcl_service_ops *ops, int unique,
			 void *servicedata, uint32_t *service_id)
{
	sm_group_t *sg;
	int found = FALSE;
	int error = -EINVAL;

	if (level > SG_LEVELS - 1)
		goto fail;

	if (namelen > MAX_SERVICE_NAME_LEN)
		goto fail;

	error = kcl_addref_cluster();
	if (error)
		goto fail;

	down(&sm_sglock);

	list_for_each_entry(sg, &sm_sg[level], list) {
		if ((sg->namelen == namelen) &&
		    (!strncmp(sg->name, name, namelen))) {
			found = TRUE;
			goto next;
		}
	}

	list_for_each_entry(sg, &sg_registered[level], list) {
		if ((sg->namelen == namelen) &&
		    (!strncmp(sg->name, name, namelen))) {
			found = TRUE;
			goto next;
		}
	}

      next:

	if (found && unique) {
		error = -EEXIST;
		goto fail_unlock;
	}

	if (found) {
		sg->refcount++;
		goto out;
	}

	sg = (sm_group_t *) kmalloc(sizeof(sm_group_t) + namelen, GFP_KERNEL);
	if (!sg) {
		error = -ENOMEM;
		goto fail_unlock;
	}
	memset(sg, 0, sizeof(sm_group_t) + namelen);

	sg->refcount = 1;
	sg->service_data = servicedata;
	sg->ops = ops;
	sg->level = level;
	sg->namelen = namelen;
	memcpy(sg->name, name, namelen);
	sg->local_id = sm_new_local_id(level);
	sg->state = SGST_NONE;
	INIT_LIST_HEAD(&sg->memb);
	INIT_LIST_HEAD(&sg->joining);
	init_completion(&sg->event_comp);

	list_add_tail(&sg->list, &sg_registered[level]);

      out:
	*service_id = sg->local_id;
	up(&sm_sglock);
	return 0;

      fail_unlock:
	up(&sm_sglock);
	kcl_releaseref_cluster();
      fail:
	return error;
}

/* Context: service */

void kcl_unregister_service(uint32_t local_id)
{
	sm_group_t *sg;
	int level = sm_id_to_level(local_id);

	down(&sm_sglock);

	list_for_each_entry(sg, &sg_registered[level], list) {
		if (sg->local_id == local_id) {
			SM_ASSERT(sg->refcount,);
			sg->refcount--;

			if (!sg->refcount) {
				list_del(&sg->list);
				kfree(sg);
			}
			kcl_releaseref_cluster();
			break;
		}
	}
	up(&sm_sglock);
}

/* Context: service */

int kcl_join_service(uint32_t local_id)
{
	sm_group_t *sg;
	sm_sevent_t *sev;
	int level = sm_id_to_level(local_id);
	int error, found = FALSE;

	down(&sm_sglock);

	list_for_each_entry(sg, &sg_registered[level], list) {
		if (sg->local_id == local_id) {
			found = TRUE;
			break;
		}
	}

	if (!found) {
		up(&sm_sglock);
		error = -ENOENT;
		goto out;
	}

	if (sg->state != SGST_NONE) {
		up(&sm_sglock);
		error = -EINVAL;
		goto out;
	}

	sev = kmalloc(sizeof(sm_sevent_t), GFP_KERNEL);
	if (!sev) {
		up(&sm_sglock);
		error = -ENOMEM;
		goto out;
	}

	memset(sev, 0, sizeof (sm_sevent_t));
	sev->se_state = SEST_JOIN_BEGIN;
	sm_set_event_id(&sev->se_id);
	sev->se_sg = sg;
	sg->sevent = sev;
	sg->state = SGST_JOIN;
	set_bit(SGFL_SEVENT, &sg->flags);
	list_del(&sg->list);
	list_add_tail(&sg->list, &sm_sg[sg->level]);

	up(&sm_sglock);

	/*
	 * The join is a service event which will be processed asynchronously.
	 */

	new_joinleave(sev);
	wait_for_completion(&sg->event_comp);
	error = 0;

      out:
	return error;
}

/* Context: service */

int kcl_leave_service(uint32_t local_id)
{
	sm_group_t *sg = NULL;
	sm_sevent_t *sev;
	int error;

	error = -ENOENT;
	sg = sm_local_id_to_sg(local_id);
	if (!sg)
		goto out;

	/* sg was never joined */
	error = -EINVAL;
	if (sg->state == SGST_NONE)
		goto out;

	down(&sm_sglock);

	/* may still be joining */
	if (test_and_set_bit(SGFL_SEVENT, &sg->flags)) {
		up(&sm_sglock);
		error = -EBUSY;
		goto out;
	}

	sev = kmalloc(sizeof(sm_sevent_t), GFP_KERNEL);
	if (!sev) {
		up(&sm_sglock);
		error = -ENOMEM;
		goto out;
	}

	memset(sev, 0, sizeof (sm_sevent_t));
	sev->se_state = SEST_LEAVE_BEGIN;
	sm_set_event_id(&sev->se_id);
	set_bit(SEFL_LEAVE, &sev->se_flags);
	sev->se_sg = sg;
	sg->sevent = sev;

	up(&sm_sglock);

	new_joinleave(sev);
	wait_for_completion(&sg->event_comp);
	error = 0;

	down(&sm_sglock);
	list_del(&sg->list);
	list_add_tail(&sg->list, &sg_registered[sg->level]);
	up(&sm_sglock);

      out:
	return error;
}

static void process_callback(uint32_t local_id, int event_id)
{
	sm_group_t *sg;
	sm_sevent_t *sev;
	sm_uevent_t *uev;

	sg = sm_local_id_to_sg(local_id);
	if (!sg)
		return;

	if (sg->state == SGST_RECOVER) {
		if (!check_recovery(sg, event_id)) {
			log_error(sg, "process_callback invalid recover "
				  "event id %d", event_id);
			return;
		}

		log_debug(sg, "cb recover state %u", sg->recover_state);

		if (sg->recover_state == RECOVER_START)
			sg->recover_state = RECOVER_STARTDONE;
		else
			log_error(sg, "process_callback recover state %u",
				  sg->recover_state);
		wake_serviced(DO_RECOVERIES);
	}

	else if (test_bit(SGFL_SEVENT, &sg->flags) && sg->sevent &&
		 (sg->sevent->se_id == event_id)) {
		sev = sg->sevent;

		if (test_and_clear_bit(SEFL_ALLOW_STARTDONE, &sev->se_flags) &&
		    (sev->se_state == SEST_JSTART_SERVICEWAIT))
			sev->se_state = SEST_JSTART_SERVICEDONE;

		set_bit(SEFL_CHECK, &sev->se_flags);
		wake_serviced(DO_JOINLEAVE);
	}

	else if (test_bit(SGFL_UEVENT, &sg->flags) &&
		 (sg->uevent.ue_id == event_id)) {
		uev = &sg->uevent;

		if (test_and_clear_bit(UEFL_ALLOW_STARTDONE, &uev->ue_flags)) {
			if (uev->ue_state == UEST_JSTART_SERVICEWAIT)
				uev->ue_state = UEST_JSTART_SERVICEDONE;
			else if (uev->ue_state == UEST_LSTART_SERVICEWAIT)
				uev->ue_state = UEST_LSTART_SERVICEDONE;
		}
		set_bit(UEFL_CHECK, &uev->ue_flags);
		wake_serviced(DO_MEMBERSHIP);
	}

	else
		log_error(sg, "ignoring service callback id=%x event=%u",
			  local_id, event_id);
}

void process_callbacks(void)
{
	sc_entry_t *se;

	while (1) {
		se = NULL;

		spin_lock(&callback_lock);
		if (!list_empty(&callbacks)) {
			se = list_entry(callbacks.next, sc_entry_t, list);
			list_del(&se->list);
		}
		spin_unlock(&callback_lock);

		if (!se)
			break;
		process_callback(se->local_id, se->event_id);
		kfree(se);
		schedule();
	}
}

/* Context: service */

void kcl_start_done(uint32_t local_id, int event_id)
{
	sc_entry_t *se;

	SM_RETRY(se = kmalloc(sizeof(sc_entry_t), GFP_KERNEL), se);

	se->local_id = local_id;
	se->event_id = event_id;

	spin_lock(&callback_lock);
	list_add_tail(&se->list, &callbacks);
	spin_unlock(&callback_lock);

	wake_serviced(DO_CALLBACKS);
}

/* Context: service */

void kcl_global_service_id(uint32_t local_id, uint32_t *global_id)
{
	sm_group_t *sg = sm_local_id_to_sg(local_id);

	if (!sg)
		log_print("kcl_global_service_id: can't find %x", local_id);
	else
		*global_id = sg->global_id;
}

static void copy_to_service(sm_group_t *sg, struct kcl_service *s)
{
	s->level = sg->level;
	s->local_id = sg->local_id;
	s->global_id = sg->global_id;
	s->node_count = sg->memb_count;
	strcpy(s->name, sg->name);
}

int kcl_get_services(struct list_head *head, int level)
{
	sm_group_t *sg;
	struct kcl_service *s;
	int error = -ENOMEM, count = 0;

	down(&sm_sglock);

	list_for_each_entry(sg, &sg_registered[level], list) {
		if (head) {
			s = kmalloc(sizeof(struct kcl_service), GFP_KERNEL);
			if (!s)
				goto out;
			copy_to_service(sg, s);
			list_add(&s->list, head);
		}
		count++;
	}

	list_for_each_entry(sg, &sm_sg[level], list) {
		if (head) {
			s = kmalloc(sizeof(struct kcl_service), GFP_KERNEL);
			if (!s)
				goto out;
			copy_to_service(sg, s);
			list_add(&s->list, head);
		}
		count++;
	}

	error = count;
 out:
	up(&sm_sglock);
	return error;
}

/* These three global variables listed in extern form in sm.h. */
struct list_head sm_sg[SG_LEVELS];
struct semaphore sm_sglock;
