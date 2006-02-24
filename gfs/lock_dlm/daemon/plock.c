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

#include <sys/types.h>
#include <asm/types.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <fcntl.h>
#include <netdb.h>
#include <limits.h>
#include <unistd.h>

#include "lock_dlm.h"
#include "lock_dlm_plock.h"

#define PROC_MISC               "/proc/misc"
#define PROC_DEVICES            "/proc/devices"
#define MISC_NAME               "misc"
#define CONTROL_DIR             "/dev/misc"
#define CONTROL_NAME            "lock_dlm_plock"

static int control_fd = -1;
extern int our_nodeid;

int send_plock_message(struct mountgroup *mg, int len, char *buf);

struct resource {
	struct list_head	list;	   /* list of resources */
	uint64_t		number;
	struct list_head	locks;	  /* one lock for each range */
	struct list_head	waiters;
};

struct posix_lock {
	struct list_head	list;	   /* resource locks or waiters list */
	uint32_t		pid;
	uint64_t		start;
	uint64_t		end;
	int			ex;
};

struct lock_waiter {
	struct list_head	list;
	struct gdlm_plock_info	info;
};

static int get_proc_number(const char *file, const char *name, uint32_t *number)
{
	FILE *fl;
	char nm[256];
	int c;

	if (!(fl = fopen(file, "r"))) {
		log_error("%s: fopen failed: %s", file, strerror(errno));
		return 0;
	}

	while (!feof(fl)) {
		if (fscanf(fl, "%d %255s\n", number, &nm[0]) == 2) {
			if (!strcmp(name, nm)) {
				fclose(fl);
				return 1;
			}
		} else do {
			c = fgetc(fl);
		} while (c != EOF && c != '\n');
	}
	fclose(fl);

	log_error("%s: No entry for %s found", file, name);
	return 0;
}

static int control_device_number(uint32_t *major, uint32_t *minor)
{
	if (!get_proc_number(PROC_DEVICES, MISC_NAME, major) ||
	    !get_proc_number(PROC_MISC, GDLM_PLOCK_MISC_NAME, minor)) {
		*major = 0;
		return 0;
	}

	return 1;
}

/*
 * Returns 1 if exists; 0 if it doesn't; -1 if it's wrong
 */
static int control_exists(const char *control, uint32_t major, uint32_t minor)
{
	struct stat buf;

	if (stat(control, &buf) < 0) {
		if (errno != ENOENT)
			log_error("%s: stat failed: %s", control,
				  strerror(errno));
		return 0;
	}

	if (!S_ISCHR(buf.st_mode)) {
		log_error("%s: Wrong inode type", control);
		if (!unlink(control))
			return 0;
		log_error("%s: unlink failed: %s", control, strerror(errno));
		return -1;
	}

	if (major && buf.st_rdev != makedev(major, minor)) {
		log_error("%s: Wrong device number: (%u, %u) instead of "
			  "(%u, %u)", control, major(buf.st_mode),
			  minor(buf.st_mode), major, minor);
		if (!unlink(control))
			return 0;
		log_error("%s: unlink failed: %s", control, strerror(errno));
		return -1;
	}

	return 1;
}

static int create_control(const char *control, uint32_t major, uint32_t minor)
{
	int ret;
	mode_t old_umask;

	if (!major)
		return 0;

	old_umask = umask(0022);
	ret = mkdir(CONTROL_DIR, 0777);
	umask(old_umask);
	if (ret < 0 && errno != EEXIST) {
		log_error("%s: mkdir failed: %s", CONTROL_DIR, strerror(errno));
		return 0;
	}

	if (mknod(control, S_IFCHR | S_IRUSR | S_IWUSR, makedev(major, minor)) < 0) {
		log_error("%s: mknod failed: %s", control, strerror(errno));
		return 0;
	}

	return 1;
}

static int open_control(void)
{
	char control[PATH_MAX];
	uint32_t major = 0, minor = 0;

	if (control_fd != -1)
		return 0;

	snprintf(control, sizeof(control), "%s/%s", CONTROL_DIR, CONTROL_NAME);

	if (!control_device_number(&major, &minor)) {
		log_error("Is dlm missing from kernel?");
		return -1;
	}

	if (!control_exists(control, major, minor) &&
	    !create_control(control, major, minor)) {
		log_error("Failure to communicate with kernel lock_dlm");
		return -1;
	}

	control_fd = open(control, O_RDWR);
	if (control_fd < 0) {
		log_error("Failure to communicate with kernel lock_dlm: %s",
			  strerror(errno));
		return -1;
	}

	return 0;
}

int setup_plocks(void)
{
	int rv;

	rv = open_control();
	if (rv)
		return rv;

	log_debug("plocks %d", control_fd);

	return control_fd;
}

int process_plocks(void)
{
	struct mountgroup *mg;
	struct gdlm_plock_info info;
	struct gdlm_header *hd;
	char *buf;
	int len, rv;

	memset(&info, 0, sizeof(info));

	rv = read(control_fd, &info, sizeof(info));

	log_debug("process_plocks %d op %d fs %x num %llx ex %d wait %d", rv,
		  info.optype, info.fsid, info.number, info.ex, info.wait);

	mg = find_mg_id(info.fsid);
	if (!mg) {
		rv = -EEXIST;
		goto fail;
	}

	len = sizeof(struct gdlm_header) + sizeof(struct gdlm_plock_info);
	buf = malloc(len);
	if (!buf) {
		rv = -ENOMEM;
		goto fail;
	}

	/* FIXME: do byte swapping */

	hd = (struct gdlm_header *)buf;
	hd->type = MSG_PLOCK;
	hd->nodeid = our_nodeid;
	hd->to_nodeid = 0;   /* to all */
	memcpy(buf + sizeof(struct gdlm_header), &info, sizeof(info));

	rv = send_plock_message(mg, len, buf);

	free(buf);

	if (rv) {
		log_error("send plock error %d", rv);
		goto fail;
	}
	return 0;

 fail:
	info.rv = rv;
	rv = write(control_fd, &info, sizeof(info));

	return 0;
}

static struct resource *search_resource(struct mountgroup *mg, uint64_t number)
{
	struct resource *r;

	list_for_each_entry(r, &mg->resources, list) {
		if (r->number == number)
			return r;
	}
	return NULL;
}

static int find_resource(struct mountgroup *mg, uint64_t number, int create,
			 struct resource **r_out)
{
	struct resource *r = NULL;
	int rv = 0;

	r = search_resource(mg, number);
	if (r)
		goto out;

	if (create == 0) {
		rv = -ENOENT;
		goto out;
	}

	r = malloc(sizeof(struct resource));
	if (!r) {
		rv = -ENOMEM;
		goto out;
	}

	memset(r, 0, sizeof(struct resource));
	r->number = number;
	INIT_LIST_HEAD(&r->locks);
	INIT_LIST_HEAD(&r->waiters);

	list_add_tail(&r->list, &mg->resources);
 out:
	*r_out = r;
	return rv;
}

static void put_resource(struct resource *r)
{
	if (list_empty(&r->locks) && list_empty(&r->waiters)) {
		list_del(&r->list);
		free(r);
	}
}

static inline int ranges_overlap(uint64_t start1, uint64_t end1,
				 uint64_t start2, uint64_t end2)
{
	if (end1 < start2 || start1 > end2)
		return FALSE;
	return TRUE;
}

/**
 * overlap_type - returns a value based on the type of overlap
 * @s1 - start of new lock range
 * @e1 - end of new lock range
 * @s2 - start of existing lock range
 * @e2 - end of existing lock range
 *
 */

static int overlap_type(uint64_t s1, uint64_t e1, uint64_t s2, uint64_t e2)
{
	int ret;

	/*
	 * ---r1---
	 * ---r2---
	 */

	if (s1 == s2 && e1 == e2)
		ret = 0;

	/*
	 * --r1--
	 * ---r2---
	 */

	else if (s1 == s2 && e1 < e2)
		ret = 1;

	/*
	 *   --r1--
	 * ---r2---
	 */

	else if (s1 > s2 && e1 == e2)
		ret = 1;

	/*
	 *  --r1--
	 * ---r2---
	 */

	else if (s1 > s2 && e1 < e2)
		ret = 2;

	/*
	 * ---r1---  or  ---r1---  or  ---r1---
	 * --r2--	  --r2--       --r2--
	 */

	else if (s1 <= s2 && e1 >= e2)
		ret = 3;

	/*
	 *   ---r1---
	 * ---r2---
	 */

	else if (s1 > s2 && e1 > e2)
		ret = 4;

	/*
	 * ---r1---
	 *   ---r2---
	 */

	else if (s1 < s2 && e1 < e2)
		ret = 4;

	else
		ret = -1;

	return ret;
}

/* shrink the range start2:end2 by the partially overlapping start:end */

static int shrink_range2(uint64_t *start2, uint64_t *end2,
			 uint64_t start, uint64_t end)
{
	int error = 0;

	if (*start2 < start)
		*end2 = start - 1;
	else if (*end2 > end)
		*start2 =  end + 1;
	else
		error = -1;
	return error;
}

static int shrink_range(struct posix_lock *po, uint64_t start, uint64_t end)
{
	return shrink_range2(&po->start, &po->end, start, end);
}

static int is_conflict(struct resource *r, struct gdlm_plock_info *in)
{
	struct posix_lock *po;

	list_for_each_entry(po, &r->locks, list) {
		if (po->pid == in->pid)
			continue;
		if (!ranges_overlap(po->start, po->end, in->start, in->end))
			continue;

		if (in->ex || po->ex)
			return 1;
	}
	return 0;
}

static int add_lock(struct resource *r, uint32_t pid, int ex,
		    uint64_t start, uint64_t end)
{
	struct posix_lock *po;

	po = malloc(sizeof(struct posix_lock));
	if (!po)
		return -ENOMEM;
	memset(po, 0, sizeof(struct posix_lock));

	po->start = start;
	po->end = end;
	po->pid = pid;
	po->ex = ex;
	list_add_tail(&po->list, &r->locks);

	return 0;
}

/* RN within RE (and starts or ends on RE boundary)
   1. add new lock for non-overlap area of RE, orig mode
   2. convert RE to RN range and mode */

static int lock_case1(struct posix_lock *po, struct resource *r,
		      struct gdlm_plock_info *in)
{
	uint64_t start2, end2;

	/* non-overlapping area start2:end2 */
	start2 = po->start;
	end2 = po->end;
	shrink_range2(&start2, &end2, in->start, in->end);

	po->start = in->start;
	po->end = in->end;
	po->ex = in->ex;

	add_lock(r, in->pid, !in->ex, start2, end2);

	return 0;
}

/* RN within RE (RE overlaps RN on both sides)
   1. add new lock for front fragment, orig mode
   2. add new lock for back fragment, orig mode
   3. convert RE to RN range and mode */
			 
static int lock_case2(struct posix_lock *po, struct resource *r,
		      struct gdlm_plock_info *in)

{
	add_lock(r, in->pid, !in->ex, po->start, in->start - 1);
	add_lock(r, in->pid, !in->ex, in->end + 1, po->end);

	po->start = in->start;
	po->end = in->end;
	po->ex = in->ex;

	return 0;
}

static int lock_internal(struct mountgroup *mg, struct resource *r,
			 struct gdlm_plock_info *in)
{
	struct posix_lock *po, *safe;
	int rv;

	list_for_each_entry_safe(po, safe, &r->locks, list) {
		if (po->pid != in->pid)
			continue;
		if (!ranges_overlap(po->start, po->end, in->start, in->end))
			continue;

		/* existing range (RE) overlaps new range (RN) */

		switch(overlap_type(in->start, in->end, po->start, po->end)) {

		case 0:
			if (po->ex == in->ex)
				goto out;

			/* ranges the same - just update the existing lock */
			po->ex = in->ex;
			goto out;

		case 1:
			if (po->ex == in->ex)
				goto out;

			rv = lock_case1(po, r, in);
			goto out;

		case 2:
			if (po->ex == in->ex)
				goto out;

			rv = lock_case2(po, r, in);
			goto out;

		case 3:
			list_del(&po->list);
			free(po);
			break;

		case 4:
			if (po->start < in->start)
				po->end = in->start - 1;
			else
				po->start = in->end + 1;
			break;

		default:
			rv = -1;
			goto out;
		}
	}

	rv = add_lock(r, in->pid, in->ex, in->start, in->end);

 out:
	return rv;

}

static int unlock_internal(struct mountgroup *mg, struct resource *r,
			   struct gdlm_plock_info *in)
{
	struct posix_lock *po, *safe;
	int rv = 0;

	list_for_each_entry_safe(po, safe, &r->locks, list) {
		if (po->pid != in->pid)
			continue;
		if (!ranges_overlap(po->start, po->end, in->start, in->end))
			continue;

		/* existing range (RE) overlaps new range (RN) */

		switch(overlap_type(in->start, in->end, po->start, po->end)) {

		case 0:
			/* ranges the same - just remove the existing lock */

			list_del(&po->list);
			free(po);
			goto out;

		case 1:
			/* RN within RE and starts or ends on RE boundary -
			 * shrink and update RE */

			shrink_range(po, in->start, in->end);
			goto out;

		case 2:
			/* RN within RE - shrink and update RE to be front
			 * fragment, and add a new lock for back fragment */

			add_lock(r, in->pid, po->ex, in->end + 1, po->end);
			po->end = in->start - 1;
			goto out;

		case 3:
			/* RE within RN - remove RE, then continue checking
			 * because RN could cover other locks */

			list_del(&po->list);
			free(po);
			continue;

		case 4:
			/* front of RE in RN, or end of RE in RN - shrink and
			 * update RE, then continue because RN could cover
			 * other locks */

			shrink_range(po, in->start, in->end);
			continue;

		default:
			rv = -1;
			goto out;
		}
	}

 out:
	return rv;
}

static int add_waiter(struct mountgroup *mg, struct resource *r,
		      struct gdlm_plock_info *in)

{
	struct lock_waiter *w;
	w = malloc(sizeof(struct lock_waiter));
	if (!w)
		return -ENOMEM;
	memcpy(&w->info, in, sizeof(struct gdlm_plock_info));
	list_add_tail(&w->list, &r->waiters);
	return 0;
}

static void do_waiters(struct mountgroup *mg, struct resource *r)
{
	struct lock_waiter *w, *safe;
	struct gdlm_plock_info *in;
	int rv;

	list_for_each_entry_safe(w, safe, &r->waiters, list) {
		in = &w->info;

		if (is_conflict(r, in))
			continue;

		list_del(&w->list);

		rv = lock_internal(mg, r, in);

		free(w);
	}
}

static int do_lock(struct mountgroup *mg, struct gdlm_plock_info *in)
{
	struct resource *r = NULL;
	int rv;

	rv = find_resource(mg, in->number, 1, &r);
	if (rv || !r)
		goto out;

	if (is_conflict(r, in)) {
		if (!in->wait)
			rv = -EAGAIN;
		else
			rv = add_waiter(mg, r, in);
		goto out;
	}

	rv = lock_internal(mg, r, in);
	if (rv)
		goto out;

	do_waiters(mg, r);
	put_resource(r);
 out:
	return rv;
}

static int do_unlock(struct mountgroup *mg, struct gdlm_plock_info *in)
{
	struct resource *r = NULL;
	int rv;

	rv = find_resource(mg, in->number, 0, &r);
	if (rv || !r)
		goto out;

	rv = unlock_internal(mg, r, in);
	if (rv)
		goto out;

	do_waiters(mg, r);
	put_resource(r);
 out:
	return rv;
}

void receive_plock(struct mountgroup *mg, char *buf, int len, int from)
{
	struct gdlm_plock_info info;
	struct gdlm_header *hd = (struct gdlm_header *) buf;
	int rv;

	memcpy(&info, buf + sizeof(struct gdlm_header), sizeof(info));

	log_group(mg, "receive_plock %d op %d fs %x num %llx ex %d wait %d",
		  from, info.optype, info.fsid, info.number, info.ex,
		  info.wait);

	if (from != hd->nodeid) {
		log_error("receive_plock from %d vs %d", from, hd->nodeid);
		rv = -EINVAL;
		goto out;
	}

	if (info.optype == GDLM_PLOCK_OP_GET && from != our_nodeid)
		return;

	switch (info.optype) {
	case GDLM_PLOCK_OP_LOCK:
		rv = do_lock(mg, &info);
		break;
	case GDLM_PLOCK_OP_UNLOCK:
		rv = do_unlock(mg, &info);
		break;
	case GDLM_PLOCK_OP_GET:
		/* rv = do_get(mg, &info); */
		break;
	default:
		rv = -EINVAL;
	}

 out:
	if (from == our_nodeid) {
		info.rv = rv;
		rv = write(control_fd, &info, sizeof(info));
	}
}

