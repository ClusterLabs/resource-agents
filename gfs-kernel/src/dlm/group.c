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

#include "lock_dlm.h"


struct kcl_service_ops mg_ops;

/*
 * Get the node struct for a given nodeid.
 */

static dlm_node_t *find_node_by_nodeid(dlm_t *dlm, uint32_t nodeid)
{
	dlm_node_t *node;

	list_for_each_entry(node, &dlm->mg_nodes, list) {
		if (node->nodeid == nodeid)
			return node;
	}
	return NULL;
}

/*
 * Get the node struct for a given journalid.
 */

static dlm_node_t *find_node_by_jid(dlm_t *dlm, uint32_t jid)
{
	dlm_node_t *node;

	list_for_each_entry(node, &dlm->mg_nodes, list) {
		if (node->jid == jid)
			return node;
	}
	return NULL;
}

/*
 * If the given ID is clear, get it, setting to the given VALUE.  The ID is a
 * journalid, the VALUE is our nodeid.  When successful, the held ID-lock is
 * returned (in shared mode).  As long as this ID-lock is held, the journalid
 * is owned.
 */

static int id_test_and_set(dlm_t *dlm, uint32_t id, uint32_t val,
			   dlm_lock_t **lp_set)
{
	dlm_lock_t *lp = NULL;
	struct lm_lockname name;
	lm_lock_t *lock;
	char *lvb;
	uint32_t exist_val, beval;
	int error;

	name.ln_type = LM_TYPE_JID;
	name.ln_number = id;

	error = lm_dlm_get_lock(dlm, &name, &lock);
	if (error)
		goto fail;

	error = lm_dlm_hold_lvb(lock, &lvb);
	if (error)
		goto fail_put;

	lp = (dlm_lock_t *) lock;
	set_bit(LFL_IDLOCK, &lp->flags);

 retry:

	error = lm_dlm_lock_sync(lock, LM_ST_UNLOCKED, LM_ST_SHARED,
			         LM_FLAG_TRY | LM_FLAG_NOEXP);
	if (error == -EAGAIN) {
		current->state = TASK_UNINTERRUPTIBLE;
		schedule_timeout(HZ);
		goto retry;
	}
	if (error)
		goto fail_unhold;

	memcpy(&beval, lvb, sizeof(beval));
	exist_val = be32_to_cpu(beval);

	if (!exist_val) {
		/*
		 * This id is unused.  Attempt to claim it by getting EX mode
		 * and writing our nodeid into the lvb.
		 */
		error = lm_dlm_lock_sync(lock, LM_ST_SHARED, LM_ST_EXCLUSIVE,
				         LM_FLAG_TRY | LM_FLAG_NOEXP);
		if (error == -EAGAIN) {
			lm_dlm_unlock_sync(lock, LM_ST_SHARED);
			current->state = TASK_UNINTERRUPTIBLE;
			schedule_timeout(HZ);
			goto retry;
		}
		if (error)
			goto fail_unlock;

		beval = cpu_to_be32(val);
		memcpy(lvb, &beval, sizeof(beval));

		error = lm_dlm_lock_sync(lock, LM_ST_EXCLUSIVE, LM_ST_SHARED,
				         LM_FLAG_NOEXP);
		DLM_ASSERT(!error,);

		*lp_set = lp;
		error = 0;
	} else {
		/*
		 * This id is already used. It has a non-zero nodeid in the lvb
		 */
		lm_dlm_unlock_sync(lock, LM_ST_SHARED);
		lm_dlm_unhold_lvb(lock, lvb);
		lm_dlm_put_lock(lock);
		error = exist_val;
	}

	return error;

 fail_unlock:
	lm_dlm_unlock_sync(lock, LM_ST_SHARED);

 fail_unhold:
	lm_dlm_unhold_lvb(lock, lvb);

 fail_put:
	lm_dlm_put_lock(lock);

 fail:
	return error;
}

/*
 * Release a held ID-lock clearing its VALUE.  We have to acquire the lock in
 * EX again so we can write out a zeroed lvb.
 */

static void id_clear(dlm_t *dlm, dlm_lock_t *lp)
{
	lm_lock_t *lock = (lm_lock_t *) lp;
	int error;

	/*
	 * This flag means that DLM_LKF_CONVDEADLK should not be used.
	 */
	set_bit(LFL_FORCE_PROMOTE, &lp->flags);

 retry:

	error = lm_dlm_lock_sync(lock, LM_ST_SHARED, LM_ST_EXCLUSIVE,
			         LM_FLAG_TRY | LM_FLAG_NOEXP);
	if (error == -EAGAIN) {
		schedule();
		goto retry;
	}
	if (error)
		goto end;

	memset(lp->lvb, 0, DLM_LVB_LEN);
	lm_dlm_unlock_sync(lock, LM_ST_EXCLUSIVE);

 end:
	lm_dlm_unhold_lvb(lock, lp->lvb);
	lm_dlm_put_lock(lock);
}

/*
 * Get the VALUE for a given ID.  The ID is a journalid, the VALUE is a nodeid.
 */

static int id_value(dlm_t *dlm, uint32_t id, uint32_t *val)
{
	dlm_lock_t *lp = NULL;
	struct lm_lockname name;
	lm_lock_t *lock;
	char *lvb;
	uint32_t beval;
	int error;

	name.ln_type = LM_TYPE_JID;
	name.ln_number = id;

	error = lm_dlm_get_lock(dlm, &name, &lock);
	if (error)
		goto out;

	error = lm_dlm_hold_lvb(lock, &lvb);
	if (error)
		goto out_put;

	lp = (dlm_lock_t *) lock;
	set_bit(LFL_IDLOCK, &lp->flags);

      retry:

	error = lm_dlm_lock_sync(lock, LM_ST_UNLOCKED, LM_ST_SHARED,
			         LM_FLAG_TRY | LM_FLAG_NOEXP);
	if (error == -EAGAIN) {
		current->state = TASK_UNINTERRUPTIBLE;
		schedule_timeout(HZ);
		goto retry;
	}
	if (error)
		goto out_unhold;

	memcpy(&beval, lvb, sizeof(beval));
	*val = be32_to_cpu(beval);

	lm_dlm_unlock_sync(lock, LM_ST_SHARED);

	error = 0;

 out_unhold:
	lm_dlm_unhold_lvb(lock, lvb);

 out_put:
	lm_dlm_put_lock(lock);

 out:
	return error;
}

/*
 * Find an ID with a given VALUE.  The ID is a journalid, the VALUE is a
 * nodeid.
 */

static int id_find(dlm_t *dlm, uint32_t value, uint32_t *id_out)
{
	uint32_t val, id;
	int error = 0, found = FALSE;

	for (id = 0; id < dlm->max_nodes; id++) {
		error = id_value(dlm, id, &val);
		if (error)
			break;

		if (val == value) {
			*id_out = id;
			error = 0;
			found = TRUE;
			break;
		}
	}

	if (!error && !found)
		error = -ENOENT;

	return error;
}

/*
 * Get a journalid to use.  The journalid must be owned exclusively as long as
 * this fs is mounted.  Other nodes must be able to discover our nodeid as the
 * owner of the journalid.  The journalid we claim should have the lowest value
 * of all unused journalids.
 */

static int claim_jid(dlm_t *dlm)
{
	dlm_node_t *node;
	uint32_t id;
	int error = 0;

	DLM_ASSERT(dlm->our_nodeid,);

	/*
	 * Search an arbitrary number (8) past max nodes so we're sure to find
	 * one so we can let the GFS handle the "too big jid" error and fail
	 * the mount.
	 */

	for (id = 0; id < dlm->max_nodes + 8; id++) {
		error = id_test_and_set(dlm, id, dlm->our_nodeid, &dlm->jid_lock);
		if (error < 0)
			break;
		if (error > 0)
			continue;

		dlm->jid = id;
		node = find_node_by_nodeid(dlm, dlm->our_nodeid);
		node->jid = id;
		set_bit(NFL_HAVE_JID, &node->flags);
		break;
	}

	/*
	 * If we have a problem getting a jid, pick a bogus one which should
	 * cause GFS to complain and fail to mount.
	 */

	if (error) {
		printk("lock_dlm: %s: no journal id available (%d)\n",
		       dlm->fsname, error);
		dlm->jid = dlm->max_nodes + dlm->our_nodeid;
	}

	log_debug("claim_jid %u", dlm->jid);
	return 0;
}

/*
 * Release our journalid, allowing it to be used by a node subsequently
 * mounting the fs.
 */

static void release_jid(dlm_t *dlm)
{
	id_clear(dlm, dlm->jid_lock);
	dlm->jid_lock = NULL;
}

/*
 * For all nodes in the mountgroup, find the journalid being used by each.
 */

static int discover_jids(dlm_t *dlm)
{
	dlm_node_t *node;
	uint32_t id;
	int error, notfound = 0;

	list_for_each_entry(node, &dlm->mg_nodes, list) {
		if (test_bit(NFL_HAVE_JID, &node->flags))
			continue;

		error = id_find(dlm, node->nodeid, &id);
		if (error) {
			log_debug("jid for node %d not found", node->nodeid);
			notfound++;
			continue;
		}

		node->jid = id;
		set_bit(NFL_HAVE_JID, &node->flags);
	}

	return notfound;
}

/*
 * Discover the nodeid that we've been assigned by the cluster manager.
 */

static int get_our_nodeid(dlm_t *dlm)
{
	LIST_HEAD(cur_memb);
	struct kcl_cluster_node *cur_node;

	kcl_get_members(&cur_memb);

	list_for_each_entry(cur_node, &cur_memb, list) {
		if (cur_node->us) {
			dlm->our_nodeid = cur_node->node_id;
			break;
		}
	}

	while (!list_empty(&cur_memb)) {
		cur_node = list_entry(cur_memb.next, struct kcl_cluster_node,
				      list);
		list_del(&cur_node->list);
		kfree(cur_node);
	}

	return 0;
}

/* 
 * Run in dlm_async thread
 */

void process_start(dlm_t *dlm, dlm_start_t *ds)
{
	dlm_node_t *node;
	uint32_t nodeid;
	int last_stop, last_start, error, i, new = FALSE, found;


	log_debug("start c %d type %d e %d", ds->count, ds->type, ds->event_id);

	/*
	 * gfs won't do journal recoveries once it's sent us an unmount
	 */

	if (test_bit(DFL_UMOUNT, &dlm->flags)) {
		log_debug("process_start %d skip for umount", ds->event_id);
		kcl_start_done(dlm->mg_local_id, ds->event_id);
		goto out;
	}

	/* 
	 * check if first start
	 */

	if (!test_and_set_bit(DFL_GOT_NODEID, &dlm->flags)) {
		get_our_nodeid(dlm);
		if (ds->count == 1)
			set_bit(DFL_FIRST_MOUNT, &dlm->flags);
	}

	down(&dlm->mg_nodes_lock);

	/* 
	 * find nodes which are gone
	 */

	list_for_each_entry(node, &dlm->mg_nodes, list) {
		found = FALSE;
		for (i = 0; i < ds->count; i++) {
			if (node->nodeid != ds->nodeids[i])
				continue;
			found = TRUE;
			break;
		}
		
		/* node is still a member */
		if (found)
			continue;

		set_bit(NFL_NOT_MEMBER, &node->flags);

		/* no gfs recovery needed for nodes that left cleanly */
		if (ds->type != SERVICE_NODE_FAILED)
			continue;

		/* callbacks sent only for nodes in last completed MG */
		if (!test_bit(NFL_LAST_FINISH, &node->flags))
			continue;

		/* only send a single callback per node */
		if (test_and_set_bit(NFL_SENT_CB, &node->flags))
			continue;

		dlm->fscb(dlm->fsdata, LM_CB_NEED_RECOVERY, &node->jid);
		set_bit(DFL_NEED_STARTDONE, &dlm->flags);
		log_debug("cb_need_recovery jid %u", node->jid);
	}

	/*
	 * add new nodes
	 */

	for (i = 0; i < ds->count; i++) {
		nodeid = ds->nodeids[i];

		node = find_node_by_nodeid(dlm, nodeid);
		if (node)
			continue;

		DLM_RETRY(node = kmalloc(sizeof(dlm_node_t), GFP_KERNEL), node);

		memset(node, 0, sizeof(dlm_node_t));

		node->nodeid = nodeid;
		list_add(&node->list, &dlm->mg_nodes);
		new = TRUE;
	}

	up(&dlm->mg_nodes_lock);

	/*
	 * get a jid for ourself when started for first time
	 */

	if (!test_and_set_bit(DFL_HAVE_JID, &dlm->flags))
		claim_jid(dlm);
	else if (new) {
		/* give new nodes a little time to claim a jid */
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(HZ);
	}

	/* 
	 * find jid's of new nodes
	 */

	for (;;) {
		/* we don't need to do these jid lookups if this start has been
		   followed by a stop event (and thus cancelled) */

		spin_lock(&dlm->async_lock);
		last_stop = dlm->mg_last_stop;
		last_start = dlm->mg_last_start;
		spin_unlock(&dlm->async_lock);

		if (last_stop >= ds->event_id) {
			log_debug("start %d aborted", ds->event_id);
			break;
		}

		error = discover_jids(dlm);
		if (error) {
			/* Not all jids were found.  Wait for a time to let all
			   new nodes claim_jid, then try to scan for jids
			   again. */
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(HZ);
			continue;
		}
		break;
	}

	/* 
	 * tell SM we're done if there are no GFS recoveries to wait for
	 */

	if (last_start > last_stop) {
		error = 0;
		down(&dlm->mg_nodes_lock);

		list_for_each_entry(node, &dlm->mg_nodes, list) {
			if (!test_bit(NFL_SENT_CB, &node->flags))
				continue;
			error = 1;
			break;
		}
		up(&dlm->mg_nodes_lock);

		log_debug("start recovery %d", error);

		if (!error)
			kcl_start_done(dlm->mg_local_id, ds->event_id);
	} else
		log_debug("start %d stopped %d", ds->event_id, last_stop);

 out:
	clear_bit(DFL_RECOVER, &dlm->flags);
	kfree(ds->nodeids);
	kfree(ds);
}

void process_finish(dlm_t *dlm)
{
	struct list_head *tmp, *tmpsafe;
	dlm_node_t *node;
	dlm_lock_t *lp;

	spin_lock(&dlm->async_lock);
	clear_bit(DFL_BLOCK_LOCKS, &dlm->flags);

	list_for_each_safe(tmp, tmpsafe, &dlm->delayed) {
		lp = list_entry(tmp, dlm_lock_t, dlist);

		if (lp->type != QUEUE_LOCKS_BLOCKED)
			continue;

		lp->type = 0;
		list_del(&lp->dlist);
		list_add_tail(&lp->slist, &dlm->submit);

		clear_bit(LFL_DLIST, &lp->flags);
		set_bit(LFL_SLIST, &lp->flags);
	}
	spin_unlock(&dlm->async_lock);

	down(&dlm->mg_nodes_lock);

	list_for_each_safe(tmp, tmpsafe, &dlm->mg_nodes) {
		node = list_entry(tmp, dlm_node_t, list);

		if (test_bit(NFL_NOT_MEMBER, &node->flags)) {
			list_del(&node->list);
			kfree(node);
		} else
			set_bit(NFL_LAST_FINISH, &node->flags);
	}
	up(&dlm->mg_nodes_lock);

	wake_up(&dlm->wait);
}

/*
 * Run in user process
 */

int init_mountgroup(dlm_t *dlm)
{
	int error;
	int id;

	error = kcl_register_service(dlm->fsname, dlm->fnlen, SERVICE_LEVEL_GFS,
				     &mg_ops, TRUE, (void *) dlm, &id);
	if (error)
		goto out;

	dlm->mg_local_id = id;

	/* BLOCK_LOCKS is cleared when the join is finished */
	set_bit(DFL_BLOCK_LOCKS, &dlm->flags);

	error = kcl_join_service(id);
	if (error)
		goto out_unreg;

	if (test_bit(DFL_START_ERROR, &dlm->flags))
		goto out_leave;

	return 0;

 out_leave:
	kcl_leave_service(dlm->mg_local_id);

 out_unreg:
	kcl_unregister_service(id);

 out:
	printk("lock_dlm: service error %d\n", error);
	return error;
}

void release_mountgroup(dlm_t *dlm)
{
	int last_start, last_stop;

	/* this flag causes a kcl_start_done() to be sent right away for
	   any start callbacks we get from SM */

	log_debug("umount flags %lx", dlm->flags);
	set_bit(DFL_UMOUNT, &dlm->flags);

	/* gfs has done a unmount and will not call jid_recovery_done()
	   any longer so make necessary kcl_start_done() calls so
	   kcl_leave_service() will complete */

	spin_lock(&dlm->async_lock);
	last_start = dlm->mg_last_start;
	last_stop = dlm->mg_last_stop;
	spin_unlock(&dlm->async_lock);

	if ((last_start > last_stop) &&
	    test_and_clear_bit(DFL_NEED_STARTDONE, &dlm->flags)) {
		log_debug("umount doing start_done %d", last_start);
		kcl_start_done(dlm->mg_local_id, last_start);
	}

	kcl_leave_service(dlm->mg_local_id);
	kcl_unregister_service(dlm->mg_local_id);
	release_jid(dlm);
}

/*
 * Run in GFS thread
 */

void jid_recovery_done(dlm_t *dlm, unsigned int jid, unsigned int message)
{
	dlm_node_t *node;
	int last_start, last_stop;
	int remain = 0;

	log_debug("recovery_done jid %u msg %u", jid, message);

	node = find_node_by_jid(dlm, jid);
	if (!node)
		goto out;

	log_debug("recovery_done %u,%u f %lx", jid, node->nodeid, node->flags);

	if (!test_bit(NFL_SENT_CB, &node->flags))
		goto out;

	if (!test_bit(NFL_NOT_MEMBER, &node->flags))
		goto out;

	set_bit(NFL_RECOVERY_DONE, &node->flags);

	/* 
	 * when recovery is done for all nodes, we're done with the start
	 */

	down(&dlm->mg_nodes_lock);

	list_for_each_entry(node, &dlm->mg_nodes, list) {
		if (test_bit(NFL_SENT_CB, &node->flags) &&
		    !test_bit(NFL_RECOVERY_DONE, &node->flags))
			remain++;
	}
	up(&dlm->mg_nodes_lock);

	if (!remain) {
		/* don't send a start_done if there's since been a stop which
		 * cancels this start */

		spin_lock(&dlm->async_lock);
		last_start = dlm->mg_last_start;
		last_stop = dlm->mg_last_stop;
		spin_unlock(&dlm->async_lock);

		if (last_start > last_stop) {
			log_debug("recovery_done start_done %d", last_start);
			kcl_start_done(dlm->mg_local_id, last_start);
			clear_bit(DFL_NEED_STARTDONE, &dlm->flags);
		}
	}

 out:
	return;
}

/* 
 * Run in CMAN SM thread
 */

static void queue_start(dlm_t *dlm, uint32_t *nodeids, int count,
			int event_id, int type)
{
	dlm_start_t *ds;

	DLM_RETRY(ds = kmalloc(sizeof(dlm_start_t), GFP_KERNEL), ds);

	memset(ds, 0, sizeof(dlm_start_t));

	ds->nodeids = nodeids;
	ds->count = count;
	ds->event_id = event_id;
	ds->type = type;

	spin_lock(&dlm->async_lock);
	dlm->mg_last_start = event_id;
	list_add_tail(&ds->list, &dlm->starts);
	spin_unlock(&dlm->async_lock);

	wake_up(&dlm->wait);
}

static int mg_stop(void *data)
{
	dlm_t *dlm = (dlm_t *) data;

	spin_lock(&dlm->async_lock);
	set_bit(DFL_BLOCK_LOCKS, &dlm->flags);
	dlm->mg_last_stop = dlm->mg_last_start;
	spin_unlock(&dlm->async_lock);

	return 0;
}

static int mg_start(void *data, uint32_t *nodeids, int count, int event_id,
		    int type)
{
	dlm_t *dlm = (dlm_t *) data;

	queue_start(dlm, nodeids, count, event_id, type);

	return 0;
}

static void mg_finish(void *data, int event_id)
{
	dlm_t *dlm = (dlm_t *) data;

	spin_lock(&dlm->async_lock);
	dlm->mg_last_finish = event_id;
	set_bit(DFL_MG_FINISH, &dlm->flags);
	spin_unlock(&dlm->async_lock);

	wake_up(&dlm->wait);
}

struct kcl_service_ops mg_ops = {
	.stop = mg_stop,
	.start = mg_start,
	.finish = mg_finish
};
