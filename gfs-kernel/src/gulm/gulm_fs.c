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

#include "gulm.h"

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/file.h>
#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>
#include <linux/utsname.h>	/* for extern system_utsname */

#include "handler.h"
#include "gulm_lock_queue.h"
#include "gulm_jid.h"

/* things about myself */
extern gulm_cm_t gulm_cm;

/* globals for this file.*/
uint32_t filesystems_count = 0;
LIST_HEAD (filesystems_list);
struct semaphore filesystem_lck;	/* we use a sema instead of a spin
					 * here because all of the
					 * interruptible things we do
					 * inside of it.  If i stop doing
					 * nasty things within this it
					 * doesn't need to be a sema.
					 */
struct semaphore start_stop_lock;
atomic_t start_stop_cnt;

/**
 * init_gulm_fs - 
 */
void
init_gulm_fs (void)
{
	init_MUTEX (&filesystem_lck);
	init_MUTEX (&start_stop_lock);
	atomic_set (&start_stop_cnt, 0);
}

/*****************************************************************************/
struct rjrpf_s {
	gulm_fs_t *fs;
	uint8_t *name;
};

void
request_journal_replay_per_fs (void *d)
{
	struct rjrpf_s *rf = (struct rjrpf_s *) d;
	uint32_t jid;
	unsigned int ujid;

	/* lookup jid <=> name mapping */
	if (find_jid_by_name_and_mark_replay (rf->fs, rf->name, &jid) != 0) {
		log_msg (lgm_JIDMap,
			 "In fs (%s), no jid for name (%s) was found.\n",
			 rf->fs->fs_name, rf->name);
	} else {
		log_msg (lgm_JIDMap,
			 "In fs (%s), jid %d was found for name (%s).\n",
			 rf->fs->fs_name, jid, rf->name);

		/* all that the replay journal call back into gfs does is
		 * malloc some memory and add it to a list.  So we really
		 * don't need to queue that action.  Since that is what gfs
		 * is doing.
		 *
		 * This will need to change if gfs changes.
		 *
		 * Basically, we assume that the callback is non-blocking.
		 */
		ujid = jid;
		rf->fs->cb (rf->fs->fsdata, LM_CB_NEED_RECOVERY, &ujid);
	}

	kfree (rf->name);
	kfree (rf);

}

/**
 * request_journal_replay - give a journal replay request to mounted filesystems
 * @name: < the name of the node that died.
 * 
 * 
 * Returns: void
 */
void
request_journal_replay (uint8_t * name)
{
	struct list_head *tmp;
	gulm_fs_t *fs;
	struct rjrpf_s *rf;

	log_msg (lgm_Always, "Checking for journals for node \"%s\"\n",
		 name);

	down (&filesystem_lck);

	list_for_each (tmp, &filesystems_list) {
		fs = list_entry (tmp, gulm_fs_t, fs_list);

		/* we don't want to process replay requests when we are
		 * still in the first mounter state.  All the journals are
		 * getting replayed anyways, and there could be some issue
		 * with stuff happening twice.
		 */
		if (fs->firstmounting)
			continue;

		/* due to the way the new jid mapping code works, we had to
		 * move it out of here.
		 */

		rf = kmalloc (sizeof (struct rjrpf_s), GFP_KERNEL);
		GULM_ASSERT (rf != NULL,);

		rf->fs = fs;
		rf->name = kmalloc (strlen (name) + 1, GFP_KERNEL);
		GULM_ASSERT (rf->name != NULL,);
		memcpy (rf->name, name, strlen (name) + 1);

		qu_function_call (&fs->cq, request_journal_replay_per_fs, rf);

	}
	up (&filesystem_lck);
}

/**
 * passup_droplocks - 
 */
void
passup_droplocks (void)
{
	struct list_head *tmp;
	gulm_fs_t *fs;
	down (&filesystem_lck);
	list_for_each (tmp, &filesystems_list) {
		fs = list_entry (tmp, gulm_fs_t, fs_list);
		qu_drop_req (&fs->cq, fs->cb, fs->fsdata, LM_CB_DROPLOCKS, 0,
			     0);
		/* If this decides to block someday, we need to change this
		 * function.
		 */
	}
	up (&filesystem_lck);
}

/**
 * dump_internal_lists - 
 * 
 */
void
dump_internal_lists (void)
{
	struct list_head *tmp;
	gulm_fs_t *fs;
	down (&filesystem_lck);
	list_for_each (tmp, &filesystems_list) {
		fs = list_entry (tmp, gulm_fs_t, fs_list);
		log_msg (lgm_Always, "Handler queue for %s\n", fs->fs_name);
		display_handler_queue (&fs->cq);
		/* other lists? */
	}
	up (&filesystem_lck);
}

/**
 * get_fs_by_name - 
 * @name: 
 * 
 * 
 * Returns: gulm_fs_t
 */
gulm_fs_t *
get_fs_by_name (uint8_t * name)
{
	struct list_head *tmp;
	gulm_fs_t *fs = NULL;
	down (&filesystem_lck);
	list_for_each (tmp, &filesystems_list) {
		fs = list_entry (tmp, gulm_fs_t, fs_list);
		if (strcmp (name, fs->fs_name) == 0) {
			up (&filesystem_lck);
			return fs;
		}
	}
	up (&filesystem_lck);
	return NULL;
}

/*****************************************************************************/

/**
 * start_gulm_threads - 
 * @host_data: 
 * 
 * 
 * Returns: int
 */
int
start_gulm_threads (char *csnm, char *hostdata)
{
	int error = 0;

	down (&start_stop_lock);
	atomic_inc (&start_stop_cnt);
	if (atomic_read (&start_stop_cnt) == 1) {
		/* first one. get stuff going */
		strncpy (gulm_cm.clusterID, csnm, 255);
		gulm_cm.clusterID[255] = '\0';

		if (hostdata != NULL && strlen (hostdata) > 0) {
			strncpy (gulm_cm.myName, hostdata, 64);
		} else {
			strncpy (gulm_cm.myName, system_utsname.nodename, 64);
		}
		gulm_cm.myName[63] = '\0';


		error = lg_initialize (&gulm_cm.hookup, gulm_cm.clusterID,
				       "GFS Kernel Interface");
		if (error != 0) {
			log_err ("lg_initialize failed, %d\n", error);
			goto fail;
		}
		gulm_cm.starts = TRUE;

		/* breaking away from ccs. just hardcoding defaults here.
		 * Noone really used these anyways and if ppl want them
		 * badly, we'll find another way to set them. (modprobe
		 * options for example. or maybe sysfs?)
		 * */
		gulm_cm.handler_threads = 2;
		gulm_cm.verbosity = lgm_Network | lgm_Stomith | lgm_Forking;

		jid_init ();

		error = cm_login ();
		if (error != 0) {
			log_err ("cm_login failed. %d\n", error);
			goto fail;
		}
		error = glq_startup ();
		if (error != 0) {
			log_err ("glq_startup failed. %d\n", error);
			goto fail;
		}

	}
      fail:
	up (&start_stop_lock);
	return error;
}

/**
 * stop_gulm_threads - 
 */
void
stop_gulm_threads (void)
{
	down (&start_stop_lock);
	atomic_dec (&start_stop_cnt);
	if (atomic_read (&start_stop_cnt) == 0) {
		/* last one, put it all away. */
		glq_shutdown ();
		cm_logout ();
		lg_release (gulm_cm.hookup);
		gulm_cm.hookup = NULL;
		gulm_cm.GenerationID = 0;
	}
	up (&start_stop_lock);
}

/*****************************************************************************/
/**
 * send_drop_exp - 
 * @fs: 
 * @name: 
 * 
 * 
 * Returns: int
 */
int send_drop_exp (gulm_fs_t * fs, char *name)
{
	int len;
	glckr_t *item;

	item = glq_get_new_req();
	if( item == NULL ) {
		log_err("drop_exp: failed to get needed memory. skipping.\n");
		return -ENOMEM;
	}

	item->key = kmalloc(GIO_KEY_SIZE, GFP_KERNEL);
	if (item->key == NULL) {
		glq_recycle_req(item);
		log_err("drop_exp: failed to get needed memory. skipping.\n");
		return -ENOMEM;
	}
	item->keylen = pack_drop_mask(item->key, GIO_KEY_SIZE, fs->fs_name);

	/* pretent lvb is name for drops. */
	if (name != NULL) {
		len = strlen(name) +1;
		item->lvb = kmalloc(len, GFP_KERNEL);
		if (item->lvb == NULL) {
			glq_recycle_req(item); /* frees key for us */
			log_err("drop_exp: failed to get needed memory. skipping.\n");
			return -ENOMEM;
		}
		memcpy(item->lvb, name, len);
	} else {
		item->lvb = NULL;
	}

	item->subid = 0;
	item->start = 0;
	item->stop = ~((uint64_t)0);
	item->type = glq_req_type_drop;
	item->state = 0;
	item->flags = 0;
	item->error = 0;
	item->lvblen = 0;
	item->finish = NULL;

	glq_queue (item);

	return 0;
}
/*****************************************************************************/

/**
 * gulm_mount
 * @table_name: clusterID:FS_Name
 * @host_data:
 * @cb: GFS callback function
 * @fsdata: opaque GFS handle
 * @lockstruct: the structure of crap to fill in
 *
 * Returns: 0 on success, -EXXX on failure
 */
int
gulm_mount (char *table_name, char *host_data,
	    lm_callback_t cb, lm_fsdata_t * fsdata,
	    unsigned int min_lvb_size, struct lm_lockstruct *lockstruct)
{
	gulm_fs_t *gulm;
	char *work=NULL, *tbln;
	int first;
	int error = -1;
	struct list_head *lltmp;

	work = kmalloc(256, GFP_KERNEL);
	if(work == NULL ) {
		log_err("Out of Memory.\n");
		error = -ENOMEM;
		goto fail;
	}
	strncpy (work, table_name, 256);

	tbln = strstr (work, ":");
	if (tbln == NULL) {
		log_err
		    ("Malformed table name. Couldn't find separator ':' between "
		     "clusterID and lockspace name.\n");
		error = -1;
		goto fail;
	}
	*tbln++ = '\0';

	/* make sure that the cluster name exists. */
	if (strlen (work) <= 0) {
		log_err ("Cluster name \"%s\" is too short.\n", work);
		error = -EPROTO;
		goto fail;
	}
	if (strlen (work) > 16) {
		log_err ("Cluster name \"%s\" is too long.\n", work);
		error = -EPROTO;
		goto fail;
	}

	/* the second one is an artifact of the way I use the name.  
	 * A better fix to this will happen when I actually get dynamic key
	 * lengths working.
	 */
	if (strlen (tbln) > MIN (GIO_NAME_LEN, (GIO_KEY_SIZE - 15))) {
		log_err
		    ("Warning! lockspace name (%s) is longer than %d chars!\n",
		     tbln, MIN (GIO_NAME_LEN, (GIO_KEY_SIZE - 15)));
		error = -EPROTO;
		goto fail;
	}
	if (strlen (tbln) <= 0) {
		log_err ("Table name \"%s\" is too short.\n", tbln);
		error = -EPROTO;
		goto fail;
	}

	/*  Check to make sure this lock table isn't already being used  */
	down (&filesystem_lck);
	list_for_each (lltmp, &filesystems_list) {
		gulm = list_entry (lltmp, gulm_fs_t, fs_list);
		if (!strncmp (gulm->fs_name, tbln, GIO_NAME_LEN)) {
			log_err ("\"%s\" is already in use\n", tbln);
			error = -EEXIST;
			up (&filesystem_lck);
			goto fail;
		}
	}
	up (&filesystem_lck);

	/*  Set up our main structure  */

	gulm = kmalloc (sizeof (gulm_fs_t), GFP_KERNEL);
	if (!gulm) {
		log_err ("out of memory\n");
		error = -ENOMEM;
		goto fail;
	}
	memset (gulm, 0, sizeof (gulm_fs_t));

	INIT_LIST_HEAD (&gulm->fs_list);

	strncpy (gulm->fs_name, tbln, GIO_NAME_LEN);
	gulm->cb = cb;
	gulm->fsdata = fsdata;
	gulm->lvb_size = min_lvb_size;

	if ((error = start_gulm_threads (work, host_data)) != 0) {
		log_err ("Got a %d trying to start the threads.\n", error);
		goto fail_free_gulm;
	}

	if ((error =
	     start_callback_qu (&gulm->cq, gulm_cm.handler_threads)) < 0) {
		log_err ("fsid=%s: Failed to start the callback handler.\n",
			 gulm->fs_name);
		goto fail_free_gulm;
	}

	/* the mount lock HAS to be the first thing done in the LTs for this fs. */
	error = get_mount_lock (gulm, &first);
	if (error != 0) {
		log_err
		    ("fsid=%s: Error %d while trying to get the mount lock\n",
		     gulm->fs_name, error);
		goto fail_callback;
	}

	jid_lockstate_reserve (gulm, first);
	jid_fs_init (gulm);
	get_journalID (gulm);

	/* things act a bit different until the first mounter is finished.
	 */
	if (first)
		gulm->firstmounting = TRUE;

	/*  Success  */
	down (&filesystem_lck);
	list_add (&gulm->fs_list, &filesystems_list);
	filesystems_count++;
	up (&filesystem_lck);

	log_msg (lgm_JIDMap, "fsid=%s: We will be using jid %d\n",
		 gulm->fs_name, gulm->fsJID);

	lockstruct->ls_jid = gulm->fsJID;
	lockstruct->ls_first = first;
	lockstruct->ls_lvb_size = gulm->lvb_size;
	lockstruct->ls_lockspace = gulm;
	lockstruct->ls_ops = &gulm_ops;
#ifdef USE_SYNC_LOCKING
	lockstruct->ls_flags = 0;

	log_msg (lgm_Network2, "Done: %s, sync mode\n", table_name);
#else
	lockstruct->ls_flags = LM_LSFLAG_ASYNC;

	log_msg (lgm_Network2, "Done: %s, async mode\n", table_name);
#endif

	gulm_cm.starts = FALSE;
	return 0;

      fail_callback:
	stop_callback_qu (&gulm->cq);

      fail_free_gulm:
	kfree (gulm);
	stop_gulm_threads ();

      fail:

	if(work != NULL ) kfree(work);
	gulm_cm.starts = FALSE;
	log_msg (lgm_Always, "fsid=%s: Exiting gulm_mount with errors %d\n",
		 table_name, error);
	return error;
}

/**
 * gulm_others_may_mount
 * @lockspace: handle to specific lock space
 *
 * GFS calls this function if it was the first mounter after it's done
 * checking all the journals.
 *
 */
void
gulm_others_may_mount (lm_lockspace_t * lockspace)
{
	gulm_fs_t *fs = (gulm_fs_t *) lockspace;
	int err = 0;

	/* first send the drop all exp message.
	 * */
	err = send_drop_exp (fs, NULL);
	if (err < 0)
		log_err
		    ("fsid=%s: Problems sending DropExp request to LTPX: %d\n",
		     fs->fs_name, err);

	/* then move the FirstMountLock to shared so others can mount. */
	err = downgrade_mount_lock (fs);

	if (err < 0) {
		log_err ("fsid=%s: error sending Fs_FinMount_Req.(%d)\n",
			 fs->fs_name, err);
	}

	/* first mounter is all done.  let the gulm_recovery_done function
	 * behave as normal now.
	 */
	fs->firstmounting = FALSE;
}

/**
 * gulm_umount
 * @lockspace: handle to specific lock space
 *
 */
void
gulm_unmount (lm_lockspace_t * lockspace)
{
	gulm_fs_t *gulm_fs = (gulm_fs_t *) lockspace;

	down (&filesystem_lck);
	list_del (&gulm_fs->fs_list);
	--filesystems_count;
	up (&filesystem_lck);

	/* close and release stuff */
	drop_mount_lock (gulm_fs);
	put_journalID (gulm_fs);
	jid_fs_release (gulm_fs);
	jid_lockstate_release (gulm_fs);

	stop_callback_qu (&gulm_fs->cq);

	kfree (gulm_fs);

	stop_gulm_threads ();

}

/**
 * gulm_recovery_done - 
 * @lockspace: 
 * @jid: 
 * 
 * Returns: void
 */
void
gulm_recovery_done (lm_lockspace_t * lockspace, unsigned int jid,
		    unsigned int message)
{
	gulm_fs_t *fs = (gulm_fs_t *) lockspace;
	int err;
	uint8_t name[64];

	if (message != LM_RD_SUCCESS) {
		/* Need to start thinking about how I want to use this... */
		return;
	}

	if (jid == fs->fsJID) {	/* this may be drifting crud through. */
		/* hey! its me! */
		strncpy (name, gulm_cm.myName, 64);
	} else if (lookup_name_by_jid (fs, jid, name) != 0) {
		log_msg (lgm_JIDMap,
			 "fsid=%s: Could not find a client for jid %d\n",
			 fs->fs_name, jid);
		return;
	}
	if (strlen (name) == 0) {
		log_msg (lgm_JIDMap, "fsid=%s: No one mapped to jid %d\n",
			 fs->fs_name, jid);
		return;
	}
	log_msg (lgm_JIDMap, "fsid=%s: Found %s for jid %d\n",
		 fs->fs_name, name, jid);

	err = send_drop_exp (fs, name);

	if (jid != fs->fsJID) {
		/* rather dumb to do this to ourselves right after we mount... */
		log_msg (lgm_JIDMap,
			 "fsid=%s: Clearing JID %d for use by others\n",
			 fs->fs_name, jid);
		release_JID (fs, jid, FALSE);
	}

	/* If someone died while replaying someoneelse's journal, there will be
	 * stale expired jids.
	 */
	check_for_stale_expires (fs);

}
/* vim: set ai cin noet sw=8 ts=8 : */
