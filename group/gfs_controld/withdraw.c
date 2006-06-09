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

#include "lock_dlm.h"

static dlm_lshandle_t *dh;
static int dlm_fd;
static int cb_bast;
static void *cb_arg;

extern int our_nodeid;
extern struct list_head mounts;
extern int no_withdraw;

int set_sysfs(struct mountgroup *mg, char *field, int val);
struct mg_member *find_memb_nodeid(struct mountgroup *mg, int nodeid);


void withdraw_bast(void *arg)
{
	cb_arg = arg;
	cb_bast = 1;
}

void process_bast(void)
{
	struct mountgroup *mg;
	struct mg_member *memb;
	int rv, found = 0;

	list_for_each_entry(mg, &mounts, list) {
		list_for_each_entry(memb, &mg->members, list) {
			if ((void *) memb == cb_arg) {
				found = 1;
				break;
			}
		}
		if (found)
			break;
	}

	if (!found) {
		log_error("withdraw bast for unknown member %p", cb_arg);
		return;
	}

	log_group(mg, "process_bast %d withdrawing", memb->nodeid);

	memb->withdraw = 1;

	rv = set_sysfs(mg, "block", 1);
	if (rv) {
		log_error("set_sysfs block error %d", rv);
		return;
	}

	rv = dlm_ls_unlock_wait(dh, memb->wd_lksb.sb_lkid, 0, &memb->wd_lksb);

	if (rv != 0)
		log_error("dlm_ls_unlock_wait %d rv %d", memb->nodeid, rv);

	memset(&memb->wd_lksb, 0, sizeof(struct dlm_lksb));
}

/* acquire each node's withdraw lock in PR, including our own */

int hold_withdraw_locks(struct mountgroup *mg)
{
	struct mg_member *memb;
	char name[DLM_RESNAME_MAXLEN];
	int rv;

	if (no_withdraw)
		return 0;

	log_group(mg, "hold_withdraw_locks for new nodes");

	list_for_each_entry(memb, &mg->members, list) {
		if (memb->withdraw)
			continue;

		if (memb->wd_lksb.sb_lkid)
			continue;

		memset(name, 0, sizeof(name));
		snprintf(name, sizeof(name), "%s %u", mg->name, memb->nodeid);

		rv = dlm_ls_lock_wait(dh, LKM_PRMODE, &memb->wd_lksb,
				      LKF_NOQUEUE, name, sizeof(name),
				      0, (void *) memb, withdraw_bast, NULL);

		if (rv != 0)
			log_error("hold_withdraw_locks %d rv %d",
				  memb->nodeid, rv);

		if (memb->wd_lksb.sb_status != 0)
			log_error("hold_withdraw_locks %d sb_status %d",
				  memb->nodeid, memb->wd_lksb.sb_status);

		log_group(mg, "hold withdraw %d lkid %x", memb->nodeid,
			  memb->wd_lksb.sb_lkid);
	}
	return 0;
}

void release_withdraw_lock(struct mountgroup *mg, struct mg_member *memb)
{
	char name[DLM_RESNAME_MAXLEN];
	int rv;
	uint32_t lkid;

	if (no_withdraw)
		return;

	memset(name, 0, sizeof(name));
	snprintf(name, sizeof(name), "%s %u", mg->name, memb->nodeid);
	lkid = memb->wd_lksb.sb_lkid;

	if (!lkid) {
		log_group(mg, "release withdraw %d skip", memb->nodeid);
		return;
	}

	rv = dlm_ls_unlock_wait(dh, lkid, 0, &memb->wd_lksb);
	if (rv != 0)
		log_error("release_withdraw_lock %d rv %d", memb->nodeid, rv);
	log_group(mg, "release withdraw %d lkid %x", memb->nodeid, lkid);
	memb->wd_lksb.sb_lkid = 0;
}

void release_withdraw_locks(struct mountgroup *mg)
{
	struct mg_member *memb;

	if (no_withdraw)
		return;

	list_for_each_entry(memb, &mg->members, list)
		release_withdraw_lock(mg, memb);
}

int promote_withdraw_lock(struct mountgroup *mg)
{
	struct mg_member *memb;
	char name[DLM_RESNAME_MAXLEN];
	int rv;

	if (no_withdraw)
		return 0;

	memb = find_memb_nodeid(mg, our_nodeid);
	if (!memb) {
		log_error("our_nodeid not found %d", our_nodeid); 
		rv = -1;
		goto out;
	}

	memset(name, 0, sizeof(name));
	snprintf(name, sizeof(name), "%s %u", mg->name, memb->nodeid);

	rv = dlm_ls_lock_wait(dh, LKM_EXMODE, &memb->wd_lksb, LKF_CONVERT,
			      name, sizeof(name), 0, NULL, NULL, NULL);
	if (rv != 0) {
		log_error("promote_withdraw_lock rv %d", rv); 
		goto out;
	}

	log_group(mg, "promoted our withdraw lock");
 out:
	return rv;
}

int do_withdraw(char *table)
{
	struct mountgroup *mg;
	char *name = strstr(table, ":") + 1;
	int rv;

	if (no_withdraw) {
		log_error("withdraw feature not enabled");
		return 0;
	}

	mg = find_mg(name);
	if (!mg) {
		log_error("do_withdraw no mountgroup %s", name);
		return -1;
	}

	rv = promote_withdraw_lock(mg);
	if (rv) {
		log_error("do_withdraw promote error %d", rv);
		goto out;
	}

	rv = set_sysfs(mg, "withdraw", 1);
	if (rv) {
		log_error("do_withdraw set_sysfs error %d", rv);
		goto out;
	}

	mg->withdraw = 1;

	/* FIXME: now just go ahead and leave the mountgroup, leaving
	   enough around so that when umount.gfs2 is called we can
	   do the formal umount(2). */

 out:
	return rv;
}

int process_libdlm(void)
{
	dlm_dispatch(dlm_fd);

	if (cb_bast) {
		cb_bast = 0;
		process_bast();
	}
	return 0;
}

int setup_libdlm(void)
{
	int rv;

	dh = dlm_create_lockspace("gfs_controld", 0600);
	if (!dh) {
		log_error("dlm_create_lockspace error %d %d", (int) dh, errno);
		return -ENOTCONN;
	}

	rv = dlm_ls_get_fd(dh);
	if (rv < 0)
		log_error("dlm_ls_get_fd error %d %d", rv, errno);
	else
		dlm_fd = rv;

	log_debug("libdlm %d", rv);

	return rv;
}

void exit_libdlm(void)
{
	int rv;

	rv = dlm_release_lockspace("gfs_controld", dh, 1);
	if (rv < 0)
		log_error("dlm_release_lockspace error %d %d", rv, errno);
}

