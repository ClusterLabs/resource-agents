/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005-2008 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "dlm_daemon.h"
#include "config.h"

#include <linux/dlm_plock.h>

#define PROC_MISC               "/proc/misc"
#define PROC_DEVICES            "/proc/devices"
#define MISC_NAME               "misc"
#define CONTROL_DIR             "/dev/misc"
#define CONTROL_NAME            "dlm_plock"

static uint32_t plock_read_count;
static uint32_t plock_recv_count;
static uint32_t plock_rate_delays;
static struct timeval plock_read_time;
static struct timeval plock_recv_time;
static struct timeval plock_rate_last;

static int control_fd = -1;
static SaCkptHandleT system_ckpt_handle;
static SaCkptCallbacksT callbacks = { 0, 0 };
static SaVersionT version = { 'B', 1, 1 };
static char section_buf[1024 * 1024];
static uint32_t section_len;
static int need_fsid_translation = 0;

extern int message_flow_control_on;

struct pack_plock {
	uint64_t start;
	uint64_t end;
	uint64_t owner;
	uint32_t pid;
	uint32_t nodeid;
	uint8_t ex;
	uint8_t waiter;
	uint16_t pad1;
	uint32_t pad;
};

#define R_GOT_UNOWN 0x00000001 /* have received owner=0 message */

struct resource {
	struct list_head	list;	   /* list of resources */
	uint64_t		number;
	int                     owner;     /* nodeid or 0 for unowned */
	uint32_t		flags;
	struct timeval          last_access;
	struct list_head	locks;	   /* one lock for each range */
	struct list_head	waiters;
	struct list_head        pending;   /* discovering r owner */
};

#define P_SYNCING 0x00000001 /* plock has been sent as part of sync but not
				yet received */

struct posix_lock {
	struct list_head	list;	   /* resource locks or waiters list */
	uint32_t		pid;
	uint64_t		owner;
	uint64_t		start;
	uint64_t		end;
	int			ex;
	int			nodeid;
	uint32_t		flags;
};

struct lock_waiter {
	struct list_head	list;
	uint32_t		flags;
	struct dlm_plock_info	info;
};

struct save_msg {
	struct list_head list;
	int nodeid;
	int len;
	int type;
	char buf[0];
};


static void send_own(struct lockspace *ls, struct resource *r, int owner);
static void save_pending_plock(struct lockspace *ls, struct resource *r,
			       struct dlm_plock_info *in);


static int got_unown(struct resource *r)
{
	return !!(r->flags & R_GOT_UNOWN);
}

static void info_bswap_out(struct dlm_plock_info *i)
{
	i->version[0]	= cpu_to_le32(i->version[0]);
	i->version[1]	= cpu_to_le32(i->version[1]);
	i->version[2]	= cpu_to_le32(i->version[2]);
	i->pid		= cpu_to_le32(i->pid);
	i->nodeid	= cpu_to_le32(i->nodeid);
	i->rv		= cpu_to_le32(i->rv);
	i->fsid		= cpu_to_le32(i->fsid);
	i->number	= cpu_to_le64(i->number);
	i->start	= cpu_to_le64(i->start);
	i->end		= cpu_to_le64(i->end);
	i->owner	= cpu_to_le64(i->owner);
}

static void info_bswap_in(struct dlm_plock_info *i)
{
	i->version[0]	= le32_to_cpu(i->version[0]);
	i->version[1]	= le32_to_cpu(i->version[1]);
	i->version[2]	= le32_to_cpu(i->version[2]);
	i->pid		= le32_to_cpu(i->pid);
	i->nodeid	= le32_to_cpu(i->nodeid);
	i->rv		= le32_to_cpu(i->rv);
	i->fsid		= le32_to_cpu(i->fsid);
	i->number	= le64_to_cpu(i->number);
	i->start	= le64_to_cpu(i->start);
	i->end		= le64_to_cpu(i->end);
	i->owner	= le64_to_cpu(i->owner);
}

static char *op_str(int optype)
{
	switch (optype) {
	case DLM_PLOCK_OP_LOCK:
		return "LK";
	case DLM_PLOCK_OP_UNLOCK:
		return "UN";
	case DLM_PLOCK_OP_GET:
		return "GET";
	default:
		return "??";
	}
}

static char *ex_str(int optype, int ex)
{
	if (optype == DLM_PLOCK_OP_UNLOCK || optype == DLM_PLOCK_OP_GET)
		return "-";
	if (ex)
		return "WR";
	else
		return "RD";
}

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

static int control_device_number(const char *plock_misc_name,
				 uint32_t *major, uint32_t *minor)
{
	if (!get_proc_number(PROC_DEVICES, MISC_NAME, major) ||
	    !get_proc_number(PROC_MISC, plock_misc_name, minor)) {
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

static int open_control(const char *control_name, const char *plock_misc_name)
{
	char control[PATH_MAX];
	uint32_t major = 0, minor = 0;

	if (control_fd != -1)
		return 0;

	snprintf(control, sizeof(control), "%s/%s", CONTROL_DIR, control_name);

	if (!control_device_number(plock_misc_name, &major, &minor))
		return -1;

	if (!control_exists(control, major, minor) &&
	    !create_control(control, major, minor)) {
		log_error("Failure to create device file %s", control);
		return -1;
	}

	control_fd = open(control, O_RDWR);
	if (control_fd < 0) {
		log_error("Failure to open device %s: %s", control,
			  strerror(errno));
		return -1;
	}

	return 0;
}

/*
 * In kernels before 2.6.26, plocks came from gfs2's lock_dlm module.
 * Reading plocks from there as well should allow us to use cluster3
 * on old (RHEL5) kernels.  In this case, the fsid we read in plock_info
 * structs is the mountgroup id, which we need to translate to the ls id.
 */

#define OLD_CONTROL_NAME "lock_dlm_plock"
#define OLD_PLOCK_MISC_NAME "lock_dlm_plock"

int setup_plocks(void)
{
	SaAisErrorT err;
	int rv;

	plock_read_count = 0;
	plock_recv_count = 0;
	plock_rate_delays = 0;
	gettimeofday(&plock_read_time, NULL);
	gettimeofday(&plock_recv_time, NULL);
	gettimeofday(&plock_rate_last, NULL);

	err = saCkptInitialize(&system_ckpt_handle, &callbacks, &version);
	if (err != SA_AIS_OK) {
		log_error("ckpt init error %d", err);
		cfgd_enable_plock = 0;

		/* still try to open and read the control device so that we can
		   send ENOSYS back to the kernel if it tries to do a plock */
	}


	rv = open_control(CONTROL_NAME, DLM_PLOCK_MISC_NAME);
	if (rv) {
		log_debug("setup_plocks trying old lock_dlm interface");
		rv = open_control(OLD_CONTROL_NAME, OLD_PLOCK_MISC_NAME);
		if (rv) {
			log_error("Is dlm missing from kernel?  No control device.");
			return rv;
		}
		need_fsid_translation = 1;
	}

	log_debug("plocks %d", control_fd);
	log_debug("plock cpg message size: %u bytes",
		  (unsigned int) (sizeof(struct dlm_header) +
		                  sizeof(struct dlm_plock_info)));

	return control_fd;
}

static uint32_t mg_to_ls_id(uint32_t fsid)
{
	struct lockspace *ls;
	int do_set = 1;

 retry:
	list_for_each_entry(ls, &lockspaces, list) {
		if (ls->associated_mg_id == fsid)
			return ls->global_id;
	}

	if (do_set) {
		do_set = 0;
		set_associated_id(fsid);
		goto retry;
	}

	return fsid;
}

/* FIXME: unify these two */

static unsigned long time_diff_ms(struct timeval *begin, struct timeval *end)
{
	struct timeval result;
	timersub(end, begin, &result);
	return (result.tv_sec * 1000) + (result.tv_usec / 1000);
}

static uint64_t dt_usec(struct timeval *start, struct timeval *stop)
{
	uint64_t dt;

	dt = stop->tv_sec - start->tv_sec;
	dt *= 1000000;
	dt += stop->tv_usec - start->tv_usec;
	return dt;
}

static struct resource *search_resource(struct lockspace *ls, uint64_t number)
{
	struct resource *r;

	list_for_each_entry(r, &ls->plock_resources, list) {
		if (r->number == number)
			return r;
	}
	return NULL;
}

static int find_resource(struct lockspace *ls, uint64_t number, int create,
			 struct resource **r_out)
{
	struct resource *r = NULL;
	int rv = 0;

	r = search_resource(ls, number);
	if (r)
		goto out;

	if (create == 0) {
		rv = -ENOENT;
		goto out;
	}

	r = malloc(sizeof(struct resource));
	if (!r) {
		log_error("find_resource no memory %d", errno);
		rv = -ENOMEM;
		goto out;
	}

	memset(r, 0, sizeof(struct resource));
	r->number = number;
	INIT_LIST_HEAD(&r->locks);
	INIT_LIST_HEAD(&r->waiters);
	INIT_LIST_HEAD(&r->pending);

	if (cfgd_plock_ownership)
		r->owner = -1;
	else
		r->owner = 0;

	list_add_tail(&r->list, &ls->plock_resources);
 out:
	if (r)
		gettimeofday(&r->last_access, NULL);
	*r_out = r;
	return rv;
}

static void put_resource(struct resource *r)
{
	/* with ownership, resources are only freed via drop messages */
	if (cfgd_plock_ownership)
		return;

	if (list_empty(&r->locks) && list_empty(&r->waiters)) {
		list_del(&r->list);
		free(r);
	}
}

static inline int ranges_overlap(uint64_t start1, uint64_t end1,
				 uint64_t start2, uint64_t end2)
{
	if (end1 < start2 || start1 > end2)
		return 0;
	return 1;
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

static int is_conflict(struct resource *r, struct dlm_plock_info *in, int get)
{
	struct posix_lock *po;

	list_for_each_entry(po, &r->locks, list) {
		if (po->nodeid == in->nodeid && po->owner == in->owner)
			continue;
		if (!ranges_overlap(po->start, po->end, in->start, in->end))
			continue;

		if (in->ex || po->ex) {
			if (get) {
				in->ex = po->ex;
				in->pid = po->pid;
				in->start = po->start;
				in->end = po->end;
			}
			return 1;
		}
	}
	return 0;
}

static int add_lock(struct resource *r, uint32_t nodeid, uint64_t owner,
		    uint32_t pid, int ex, uint64_t start, uint64_t end)
{
	struct posix_lock *po;

	po = malloc(sizeof(struct posix_lock));
	if (!po)
		return -ENOMEM;
	memset(po, 0, sizeof(struct posix_lock));

	po->start = start;
	po->end = end;
	po->nodeid = nodeid;
	po->owner = owner;
	po->pid = pid;
	po->ex = ex;
	list_add_tail(&po->list, &r->locks);

	return 0;
}

/* RN within RE (and starts or ends on RE boundary)
   1. add new lock for non-overlap area of RE, orig mode
   2. convert RE to RN range and mode */

static int lock_case1(struct posix_lock *po, struct resource *r,
		      struct dlm_plock_info *in)
{
	uint64_t start2, end2;
	int rv;

	/* non-overlapping area start2:end2 */
	start2 = po->start;
	end2 = po->end;
	rv = shrink_range2(&start2, &end2, in->start, in->end);
	if (rv)
		goto out;

	po->start = in->start;
	po->end = in->end;
	po->ex = in->ex;

	rv = add_lock(r, in->nodeid, in->owner, in->pid, !in->ex, start2, end2);
 out:
	return rv;
}

/* RN within RE (RE overlaps RN on both sides)
   1. add new lock for front fragment, orig mode
   2. add new lock for back fragment, orig mode
   3. convert RE to RN range and mode */
			 
static int lock_case2(struct posix_lock *po, struct resource *r,
		      struct dlm_plock_info *in)

{
	int rv;

	rv = add_lock(r, in->nodeid, in->owner, in->pid,
		      !in->ex, po->start, in->start - 1);
	if (rv)
		goto out;

	rv = add_lock(r, in->nodeid, in->owner, in->pid,
		      !in->ex, in->end + 1, po->end);
	if (rv)
		goto out;

	po->start = in->start;
	po->end = in->end;
	po->ex = in->ex;
 out:
	return rv;
}

static int lock_internal(struct lockspace *ls, struct resource *r,
			 struct dlm_plock_info *in)
{
	struct posix_lock *po, *safe;
	int rv = 0;

	list_for_each_entry_safe(po, safe, &r->locks, list) {
		if (po->nodeid != in->nodeid || po->owner != in->owner)
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

	rv = add_lock(r, in->nodeid, in->owner, in->pid,
		      in->ex, in->start, in->end);
 out:
	return rv;

}

static int unlock_internal(struct lockspace *ls, struct resource *r,
			   struct dlm_plock_info *in)
{
	struct posix_lock *po, *safe;
	int rv = 0;

	list_for_each_entry_safe(po, safe, &r->locks, list) {
		if (po->nodeid != in->nodeid || po->owner != in->owner)
			continue;
		if (!ranges_overlap(po->start, po->end, in->start, in->end))
			continue;

		/* existing range (RE) overlaps new range (RN) */

		switch (overlap_type(in->start, in->end, po->start, po->end)) {

		case 0:
			/* ranges the same - just remove the existing lock */

			list_del(&po->list);
			free(po);
			goto out;

		case 1:
			/* RN within RE and starts or ends on RE boundary -
			 * shrink and update RE */

			rv = shrink_range(po, in->start, in->end);
			goto out;

		case 2:
			/* RN within RE - shrink and update RE to be front
			 * fragment, and add a new lock for back fragment */

			rv = add_lock(r, in->nodeid, in->owner, in->pid,
				      po->ex, in->end + 1, po->end);
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

			rv = shrink_range(po, in->start, in->end);
			continue;

		default:
			rv = -1;
			goto out;
		}
	}
 out:
	return rv;
}

static int add_waiter(struct lockspace *ls, struct resource *r,
		      struct dlm_plock_info *in)

{
	struct lock_waiter *w;

	w = malloc(sizeof(struct lock_waiter));
	if (!w)
		return -ENOMEM;
	memcpy(&w->info, in, sizeof(struct dlm_plock_info));
	list_add_tail(&w->list, &r->waiters);
	return 0;
}

static void write_result(struct lockspace *ls, struct dlm_plock_info *in,
			 int rv)
{
	int err;

	if (need_fsid_translation)
		in->fsid = ls->associated_mg_id;

	in->rv = rv;
	err = write(control_fd, in, sizeof(struct dlm_plock_info));
	if (err != sizeof(struct dlm_plock_info))
		log_error("plock result write err %d errno %d", err, errno);
}

static void do_waiters(struct lockspace *ls, struct resource *r)
{
	struct lock_waiter *w, *safe;
	struct dlm_plock_info *in;
	int rv;

	list_for_each_entry_safe(w, safe, &r->waiters, list) {
		in = &w->info;

		if (is_conflict(r, in, 0))
			continue;

		list_del(&w->list);

		/*
		log_group(ls, "take waiter %llx %llx-%llx %d/%u/%llx",
			  in->number, in->start, in->end,
			  in->nodeid, in->pid, in->owner);
		*/

		rv = lock_internal(ls, r, in);

		if (in->nodeid == our_nodeid)
			write_result(ls, in, rv);

		free(w);
	}
}

static void do_lock(struct lockspace *ls, struct dlm_plock_info *in,
		    struct resource *r)
{
	int rv;

	if (is_conflict(r, in, 0)) {
		if (!in->wait)
			rv = -EAGAIN;
		else {
			rv = add_waiter(ls, r, in);
			if (rv)
				goto out;
			rv = -EINPROGRESS;
		}
	} else
		rv = lock_internal(ls, r, in);

 out:
	if (in->nodeid == our_nodeid && rv != -EINPROGRESS)
		write_result(ls, in, rv);

	do_waiters(ls, r);
	put_resource(r);
}

static void do_unlock(struct lockspace *ls, struct dlm_plock_info *in,
		      struct resource *r)
{
	int rv;

	rv = unlock_internal(ls, r, in);

	if (in->nodeid == our_nodeid)
		write_result(ls, in, rv);

	do_waiters(ls, r);
	put_resource(r);
}

/* we don't even get to this function if the getlk isn't from us */

static void do_get(struct lockspace *ls, struct dlm_plock_info *in,
		   struct resource *r)
{
	int rv;

	if (is_conflict(r, in, 1))
		rv = 1;
	else
		rv = 0;

	write_result(ls, in, rv);
}

static void save_message(struct lockspace *ls, struct dlm_header *hd, int len,
			 int from, int type)
{
	struct save_msg *sm;

	sm = malloc(sizeof(struct save_msg) + len);
	if (!sm)
		return;
	memset(sm, 0, sizeof(struct save_msg) + len);

	memcpy(&sm->buf, hd, len);
	sm->type = type;
	sm->len = len;
	sm->nodeid = from;

	log_group(ls, "save %s from %d len %d", msg_name(type), from, len);

	list_add_tail(&sm->list, &ls->saved_messages);
}

static void __receive_plock(struct lockspace *ls, struct dlm_plock_info *in,
			    int from, struct resource *r)
{
	switch (in->optype) {
	case DLM_PLOCK_OP_LOCK:
		ls->last_plock_time = time(NULL);
		do_lock(ls, in, r);
		break;
	case DLM_PLOCK_OP_UNLOCK:
		ls->last_plock_time = time(NULL);
		do_unlock(ls, in, r);
		break;
	case DLM_PLOCK_OP_GET:
		do_get(ls, in, r);
		break;
	default:
		log_error("receive_plock from %d optype %d", from, in->optype);
		if (from == our_nodeid)
			write_result(ls, in, -EINVAL);
	}
}

/* When ls members receive our options message (for our mount), one of them
   saves all plock state received to that point in a checkpoint and then sends
   us our journals message.  We know to retrieve the plock state from the
   checkpoint when we receive our journals message.  Any plocks messages that
   arrive between seeing our options message and our journals message needs to
   be saved and processed after we synchronize our plock state from the
   checkpoint.  Any plock message received while we're mounting but before we
   set save_plocks (when we see our options message) can be ignored because it
   should be reflected in the checkpointed state. */

static void _receive_plock(struct lockspace *ls, struct dlm_header *hd, int len)
{
	struct dlm_plock_info info;
	struct resource *r = NULL;
	struct timeval now;
	uint64_t usec;
	int from = hd->nodeid;
	int rv, create;

	memcpy(&info, (char *)hd + sizeof(struct dlm_header), sizeof(info));
	info_bswap_in(&info);

	log_plock(ls, "receive plock %llx %s %s %llx-%llx %d/%u/%llx w %d",
		  (unsigned long long)info.number,
		  op_str(info.optype),
		  ex_str(info.optype, info.ex),
		  (unsigned long long)info.start, (unsigned long long)info.end,
		  info.nodeid, info.pid, (unsigned long long)info.owner,
		  info.wait);

	plock_recv_count++;
	if (!(plock_recv_count % 1000)) {
		gettimeofday(&now, NULL);
		usec = dt_usec(&plock_recv_time, &now);
		log_group(ls, "plock_recv_count %u time %.3f s",
			  plock_recv_count, usec * 1.e-6);
		plock_recv_time = now;
	}

	if (info.optype == DLM_PLOCK_OP_GET && from != our_nodeid)
		return;

	if (from != hd->nodeid || from != info.nodeid) {
		log_error("receive_plock from %d header %d info %d",
			  from, hd->nodeid, info.nodeid);
		return;
	}

	create = !cfgd_plock_ownership;

	rv = find_resource(ls, info.number, create, &r);

	if (rv && cfgd_plock_ownership) {
		/* There must have been a race with a drop, so we need to
		   ignore this plock op which will be resent.  If we're the one
		   who sent the plock, we need to send_own() and put it on the
		   pending list to resend once the owner is established. */

		log_debug("receive_plock from %d no r %llx", from,
			  (unsigned long long)info.number);

		if (from != our_nodeid)
			return;

		rv = find_resource(ls, info.number, 1, &r);
		if (rv)
			return;
		send_own(ls, r, our_nodeid);
		save_pending_plock(ls, r, &info);
		return;
	}
	if (rv) {
		/* r not found, rv is -ENOENT, this shouldn't happen because
		   process_plocks() creates a resource for every op */

		log_error("receive_plock from %d no r %llx %d", from,
			  (unsigned long long)info.number, rv);
		return;
	}

	/* The owner should almost always be 0 here, but other owners may
	   be possible given odd combinations of races with drop.  Odd races to
	   worry about (some seem pretty improbable):

	   - A sends drop, B sends plock, receive drop, receive plock.
	   This is addressed above.

	   - A sends drop, B sends plock, receive drop, B reads plock
	   and sends own, receive plock, on B we find owner of -1.

	   - A sends drop, B sends two plocks, receive drop, receive plocks.
	   Receiving the first plock is the previous case, receiving the
	   second plock will find r with owner of -1.

	   - A sends drop, B sends two plocks, receive drop, C sends own,
	   receive plock, B sends own, receive own (C), receive plock,
	   receive own (B).

	   Haven't tried to cook up a scenario that would lead to the
	   last case below; receiving a plock from ourself and finding
	   we're the owner of r. */

	if (!r->owner) {
		__receive_plock(ls, &info, from, r);

	} else if (r->owner == -1) {
		log_debug("receive_plock from %d r %llx owner %d", from,
			  (unsigned long long)info.number, r->owner);

		if (from == our_nodeid)
			save_pending_plock(ls, r, &info);

	} else if (r->owner != our_nodeid) {
		/* might happen, if frequent change to log_debug */
		log_error("receive_plock from %d r %llx owner %d", from,
			  (unsigned long long)info.number, r->owner);

		if (from == our_nodeid)
			save_pending_plock(ls, r, &info);

	} else if (r->owner == our_nodeid) {
		/* might happen, if frequent change to log_debug */
		log_error("receive_plock from %d r %llx owner %d", from,
			  (unsigned long long)info.number, r->owner);

		if (from == our_nodeid)
			__receive_plock(ls, &info, from, r);
	}
}

void receive_plock(struct lockspace *ls, struct dlm_header *hd, int len)
{
	if (ls->save_plocks) {
		save_message(ls, hd, len, hd->nodeid, DLM_MSG_PLOCK);
		return;
	}

	_receive_plock(ls, hd, len);
}

static int send_struct_info(struct lockspace *ls, struct dlm_plock_info *in,
			    int msg_type)
{
	struct dlm_header *hd;
	int rv = 0, len;
	char *buf;

	len = sizeof(struct dlm_header) + sizeof(struct dlm_plock_info);
	buf = malloc(len);
	if (!buf) {
		rv = -ENOMEM;
		goto out;
	}
	memset(buf, 0, len);

	info_bswap_out(in);

	hd = (struct dlm_header *)buf;
	hd->type = msg_type;

	memcpy(buf + sizeof(struct dlm_header), in, sizeof(*in));

	dlm_send_message(ls, buf, len);

	free(buf);
 out:
	if (rv)
		log_error("send_struct_info error %d", rv);
	return rv;
}

static void send_plock(struct lockspace *ls, struct resource *r,
		       struct dlm_plock_info *in)
{
	send_struct_info(ls, in, DLM_MSG_PLOCK);
}

static void send_own(struct lockspace *ls, struct resource *r, int owner)
{
	struct dlm_plock_info info;

	/* if we've already sent an own message for this resource,
	   (pending list is not empty), then we shouldn't send another */

	if (!list_empty(&r->pending)) {
		log_debug("send_own %llx already pending",
			  (unsigned long long)r->number);
		return;
	}

	memset(&info, 0, sizeof(info));
	info.number = r->number;
	info.nodeid = owner;

	send_struct_info(ls, &info, DLM_MSG_PLOCK_OWN);
}

static void send_syncs(struct lockspace *ls, struct resource *r)
{
	struct dlm_plock_info info;
	struct posix_lock *po;
	struct lock_waiter *w;
	int rv;

	list_for_each_entry(po, &r->locks, list) {
		memset(&info, 0, sizeof(info));
		info.number    = r->number;
		info.start     = po->start;
		info.end       = po->end;
		info.nodeid    = po->nodeid;
		info.owner     = po->owner;
		info.pid       = po->pid;
		info.ex        = po->ex;

		rv = send_struct_info(ls, &info, DLM_MSG_PLOCK_SYNC_LOCK);
		if (rv)
			goto out;

		po->flags |= P_SYNCING;
	}

	list_for_each_entry(w, &r->waiters, list) {
		memcpy(&info, &w->info, sizeof(info));

		rv = send_struct_info(ls, &info, DLM_MSG_PLOCK_SYNC_WAITER);
		if (rv)
			goto out;

		w->flags |= P_SYNCING;
	}
 out:
	return;
}

static void send_drop(struct lockspace *ls, struct resource *r)
{
	struct dlm_plock_info info;

	memset(&info, 0, sizeof(info));
	info.number = r->number;

	send_struct_info(ls, &info, DLM_MSG_PLOCK_DROP);
}

/* plock op can't be handled until we know the owner value of the resource,
   so the op is saved on the pending list until the r owner is established */

static void save_pending_plock(struct lockspace *ls, struct resource *r,
			       struct dlm_plock_info *in)
{
	struct lock_waiter *w;

	w = malloc(sizeof(struct lock_waiter));
	if (!w) {
		log_error("save_pending_plock no mem");
		return;
	}
	memcpy(&w->info, in, sizeof(struct dlm_plock_info));
	list_add_tail(&w->list, &r->pending);
}

/* plock ops are on pending list waiting for ownership to be established.
   owner has now become us, so add these plocks to r */

static void add_pending_plocks(struct lockspace *ls, struct resource *r)
{
	struct lock_waiter *w, *safe;

	list_for_each_entry_safe(w, safe, &r->pending, list) {
		__receive_plock(ls, &w->info, our_nodeid, r);
		list_del(&w->list);
		free(w);
	}
}

/* plock ops are on pending list waiting for ownership to be established.
   owner has now become 0, so send these plocks to everyone */

static void send_pending_plocks(struct lockspace *ls, struct resource *r)
{
	struct lock_waiter *w, *safe;

	list_for_each_entry_safe(w, safe, &r->pending, list) {
		send_plock(ls, r, &w->info);
		list_del(&w->list);
		free(w);
	}
}

static void _receive_own(struct lockspace *ls, struct dlm_header *hd, int len)
{
	struct dlm_plock_info info;
	struct resource *r;
	int should_not_happen = 0;
	int from = hd->nodeid;
	int rv;

	memcpy(&info, (char *)hd + sizeof(struct dlm_header), sizeof(info));
	info_bswap_in(&info);

	log_plock(ls, "receive own %llx from %u owner %u",
		  (unsigned long long)info.number, hd->nodeid, info.nodeid);

	rv = find_resource(ls, info.number, 1, &r);
	if (rv)
		return;

	if (from == our_nodeid) {
		/*
		 * received our own own message
		 */

		if (info.nodeid == 0) {
			/* we are setting owner to 0 */

			if (r->owner == our_nodeid) {
				/* we set owner to 0 when we relinquish
				   ownership */
				should_not_happen = 1;
			} else if (r->owner == 0) {
				/* this happens when we relinquish ownership */
				r->flags |= R_GOT_UNOWN;
			} else {
				should_not_happen = 1;
			}

		} else if (info.nodeid == our_nodeid) {
			/* we are setting owner to ourself */

			if (r->owner == -1) {
				/* we have gained ownership */
				r->owner = our_nodeid;
				add_pending_plocks(ls, r);
			} else if (r->owner == our_nodeid) {
				should_not_happen = 1;
			} else if (r->owner == 0) {
				send_pending_plocks(ls, r);
			} else {
				/* resource is owned by other node;
				   they should set owner to 0 shortly */
			}

		} else {
			/* we should only ever set owner to 0 or ourself */
			should_not_happen = 1;
		}
	} else {
		/*
		 * received own message from another node
		 */

		if (info.nodeid == 0) {
			/* other node is setting owner to 0 */

			if (r->owner == -1) {
				/* we should have a record of the owner before
				   it relinquishes */
				should_not_happen = 1;
			} else if (r->owner == our_nodeid) {
				/* only the owner should relinquish */
				should_not_happen = 1;
			} else if (r->owner == 0) {
				should_not_happen = 1;
			} else {
				r->owner = 0;
				r->flags |= R_GOT_UNOWN;
				send_pending_plocks(ls, r);
			}

		} else if (info.nodeid == from) {
			/* other node is setting owner to itself */

			if (r->owner == -1) {
				/* normal path for a node becoming owner */
				r->owner = from;
			} else if (r->owner == our_nodeid) {
				/* we relinquish our ownership: sync our local
				   plocks to everyone, then set owner to 0 */
				send_syncs(ls, r);
				send_own(ls, r, 0);
				/* we need to set owner to 0 here because
				   local ops may arrive before we receive
				   our send_own message and can't be added
				   locally */
				r->owner = 0;
			} else if (r->owner == 0) {
				/* can happen because we set owner to 0 before
				   we receive our send_own sent just above */
			} else {
				/* do nothing, current owner should be
				   relinquishing its ownership */
			}

		} else if (info.nodeid == our_nodeid) {
			/* no one else should try to set the owner to us */
			should_not_happen = 1;
		} else {
			/* a node should only ever set owner to 0 or itself */
			should_not_happen = 1;
		}
	}

	if (should_not_happen) {
		log_error("receive_own from %u %llx info nodeid %d r owner %d",
			  from, (unsigned long long)r->number, info.nodeid,
			  r->owner);
	}
}

void receive_own(struct lockspace *ls, struct dlm_header *hd, int len)
{
	if (ls->save_plocks) {
		save_message(ls, hd, len, hd->nodeid, DLM_MSG_PLOCK_OWN);
		return;
	}

	_receive_own(ls, hd, len);
}

static void clear_syncing_flag(struct resource *r, struct dlm_plock_info *in)
{
	struct posix_lock *po;
	struct lock_waiter *w;

	list_for_each_entry(po, &r->locks, list) {
		if ((po->flags & P_SYNCING) &&
		    in->start  == po->start &&
		    in->end    == po->end &&
		    in->nodeid == po->nodeid &&
		    in->owner  == po->owner &&
		    in->pid    == po->pid &&
		    in->ex     == po->ex) {
			po->flags &= ~P_SYNCING;
			return;
		}
	}

	list_for_each_entry(w, &r->waiters, list) {
		if ((w->flags & P_SYNCING) &&
		    in->start  == w->info.start &&
		    in->end    == w->info.end &&
		    in->nodeid == w->info.nodeid &&
		    in->owner  == w->info.owner &&
		    in->pid    == w->info.pid &&
		    in->ex     == w->info.ex) {
			w->flags &= ~P_SYNCING;
			return;
		}
	}

	log_error("clear_syncing %llx no match %s %llx-%llx %d/%u/%llx",
		  (unsigned long long)r->number, in->ex ? "WR" : "RD", 
		  (unsigned long long)in->start, (unsigned long long)in->end,
		  in->nodeid, in->pid, (unsigned long long)in->owner);
}

static void _receive_sync(struct lockspace *ls, struct dlm_header *hd, int len)
{
	struct dlm_plock_info info;
	struct resource *r;
	int from = hd->nodeid;
	int rv;

	memcpy(&info, (char *)hd + sizeof(struct dlm_header), sizeof(info));
	info_bswap_in(&info);

	log_plock(ls, "receive sync %llx from %u %s %llx-%llx %d/%u/%llx",
		  (unsigned long long)info.number, from, info.ex ? "WR" : "RD",
		  (unsigned long long)info.start, (unsigned long long)info.end,
		  info.nodeid, info.pid, (unsigned long long)info.owner);

	rv = find_resource(ls, info.number, 0, &r);
	if (rv) {
		log_error("receive_sync no r %llx from %d", info.number, from);
		return;
	}

	if (from == our_nodeid) {
		/* this plock now in sync on all nodes */
		clear_syncing_flag(r, &info);
		return;
	}

	if (hd->type == DLM_MSG_PLOCK_SYNC_LOCK)
		add_lock(r, info.nodeid, info.owner, info.pid, !info.ex, 
			 info.start, info.end);
	else if (hd->type == DLM_MSG_PLOCK_SYNC_WAITER)
		add_waiter(ls, r, &info);
}

void receive_sync(struct lockspace *ls, struct dlm_header *hd, int len)
{
	if (ls->save_plocks) {
		save_message(ls, hd, len, hd->nodeid, hd->type);
		return;
	}

	_receive_sync(ls, hd, len);
}

static void _receive_drop(struct lockspace *ls, struct dlm_header *hd, int len)
{
	struct dlm_plock_info info;
	struct resource *r;
	int from = hd->nodeid;
	int rv;

	memcpy(&info, (char *)hd + sizeof(struct dlm_header), sizeof(info));
	info_bswap_in(&info);

	log_plock(ls, "receive drop %llx from %u",
		  (unsigned long long)info.number, from);

	rv = find_resource(ls, info.number, 0, &r);
	if (rv) {
		/* we'll find no r if two nodes sent drop at once */
		log_debug("receive_drop from %d no r %llx", from,
			  (unsigned long long)info.number);
		return;
	}

	if (r->owner != 0) {
		/* - A sent drop, B sent drop, receive drop A, C sent own,
		     receive drop B (this warning on C, owner -1)
	   	   - A sent drop, B sent drop, receive drop A, A sent own,
		     receive own A, receive drop B (this warning on all,
		     owner A) */
		log_debug("receive_drop from %d r %llx owner %d", from,
			  (unsigned long long)r->number, r->owner);
		return;
	}

	if (!list_empty(&r->pending)) {
		/* shouldn't happen */
		log_error("receive_drop from %d r %llx pending op", from,
			  (unsigned long long)r->number);
		return;
	}

	/* the decision to drop or not must be based on things that are
	   guaranteed to be the same on all nodes */

	if (list_empty(&r->locks) && list_empty(&r->waiters)) {
		list_del(&r->list);
		free(r);
	} else {
		/* A sent drop, B sent a plock, receive plock, receive drop */
		log_debug("receive_drop from %d r %llx in use", from,
			  (unsigned long long)r->number);
	}
}

void receive_drop(struct lockspace *ls, struct dlm_header *hd, int len)
{
	if (ls->save_plocks) {
		save_message(ls, hd, len, hd->nodeid, DLM_MSG_PLOCK_DROP);
		return;
	}

	_receive_drop(ls, hd, len);
}

/* We only drop resources from the unowned state to simplify things.
   If we want to drop a resource we own, we unown/relinquish it first. */

/* FIXME: in the transition from owner = us, to owner = 0, to drop;
   we want the second period to be shorter than the first */

static int drop_resources(struct lockspace *ls)
{
	struct resource *r;
	struct timeval now;
	int count = 0;

	gettimeofday(&now, NULL);

	/* try to drop the oldest, unused resources */

	list_for_each_entry_reverse(r, &ls->plock_resources, list) {
		if (count >= cfgd_drop_resources_count)
			break;
		if (r->owner && r->owner != our_nodeid)
			continue;
		if (time_diff_ms(&r->last_access, &now) <
		    cfgd_drop_resources_age)
			continue;

		if (list_empty(&r->locks) && list_empty(&r->waiters)) {
			if (r->owner == our_nodeid) {
				send_own(ls, r, 0);
				r->owner = 0;
			} else if (r->owner == 0 && got_unown(r)) {
				send_drop(ls, r);
			}

			count++;
		}
	}

	return 0;
}

int limit_plocks(void)
{
	struct timeval now;

	/* Don't send more messages while the cpg message queue is backed up */

	if (message_flow_control_on) {
		update_flow_control_status();
		if (message_flow_control_on)
			return 1;
	}

	if (!cfgd_plock_rate_limit || !plock_read_count)
		return 0;

	gettimeofday(&now, NULL);

	/* Every time a plock op is read from the kernel, we increment
	   plock_read_count.  After every cfgd_plock_rate_limit (N) reads,
	   we check the time it's taken to do those N; if the time is less than
	   a second, then we delay reading any more until a second is up.
	   This way we read a max of N ops from the kernel every second. */

	if (!(plock_read_count % cfgd_plock_rate_limit)) {
		if (time_diff_ms(&plock_rate_last, &now) < 1000) {
			plock_rate_delays++;
			return 2;
		}
		plock_rate_last = now;
	}
	return 0;
}

void process_plocks(int ci)
{
	struct lockspace *ls;
	struct resource *r;
	struct dlm_plock_info info;
	struct timeval now;
	uint64_t usec;
	int rv;

	if (limit_plocks()) {
		poll_ignore_plock = 1;
		client_ignore(plock_ci, plock_fd);
		return;
	}

	memset(&info, 0, sizeof(info));

	rv = do_read(control_fd, &info, sizeof(info));
	if (rv < 0) {
		log_debug("process_plocks: read error %d fd %d\n",
			  errno, control_fd);
		return;
	}

	/* kernel doesn't set the nodeid field */
	info.nodeid = our_nodeid;

	if (!cfgd_enable_plock) {
		rv = -ENOSYS;
		goto fail;
	}

	if (need_fsid_translation)
		info.fsid = mg_to_ls_id(info.fsid);

	ls = find_ls_id(info.fsid);
	if (!ls) {
		log_debug("process_plocks: no ls id %x", info.fsid);
		rv = -EEXIST;
		goto fail;
	}

	log_plock(ls, "read plock %llx %s %s %llx-%llx %d/%u/%llx w %d",
		  (unsigned long long)info.number,
		  op_str(info.optype),
		  ex_str(info.optype, info.ex),
		  (unsigned long long)info.start, (unsigned long long)info.end,
		  info.nodeid, info.pid, (unsigned long long)info.owner,
		  info.wait);

	/* report plock rate and any delays since the last report */
	plock_read_count++;
	if (!(plock_read_count % 1000)) {
		usec = dt_usec(&plock_read_time, &now) ;
		log_group(ls, "plock_read_count %u time %.3f s delays %u",
			  plock_read_count, usec * 1.e-6, plock_rate_delays);
		plock_read_time = now;
		plock_rate_delays = 0;
	}

	rv = find_resource(ls, info.number, 1, &r);
	if (rv)
		goto fail;

	if (r->owner == 0) {
		/* plock state replicated on all nodes */
		send_plock(ls, r, &info);

	} else if (r->owner == our_nodeid) {
		/* we are the owner of r, so our plocks are local */
		__receive_plock(ls, &info, our_nodeid, r);

	} else {
		/* r owner is -1: r is new, try to become the owner;
		   r owner > 0: tell other owner to give up ownership;
		   both done with a message trying to set owner to ourself */
		send_own(ls, r, our_nodeid);
		save_pending_plock(ls, r, &info);
	}

	if (cfgd_plock_ownership &&
	    time_diff_ms(&ls->drop_resources_last, &now) >=
	    		 cfgd_drop_resources_time) {
		ls->drop_resources_last = now;
		drop_resources(ls);
	}

	return;

 fail:
	info.rv = rv;
	write(control_fd, &info, sizeof(info));
}

void process_saved_plocks(struct lockspace *ls)
{
	struct save_msg *sm, *sm2;
	struct dlm_header *hd;

	if (list_empty(&ls->saved_messages))
		return;

	log_group(ls, "process_saved_plocks");

	list_for_each_entry_safe(sm, sm2, &ls->saved_messages, list) {
		hd = (struct dlm_header *)sm->buf;

		switch (sm->type) {
		case DLM_MSG_PLOCK:
			_receive_plock(ls, hd, sm->len);
			break;
		case DLM_MSG_PLOCK_OWN:
			_receive_own(ls, hd, sm->len);
			break;
		case DLM_MSG_PLOCK_DROP:
			_receive_drop(ls, hd, sm->len);
			break;
		case DLM_MSG_PLOCK_SYNC_LOCK:
		case DLM_MSG_PLOCK_SYNC_WAITER:
			_receive_sync(ls, hd, sm->len);
			break;
		default:
			continue;
		}

		list_del(&sm->list);
		free(sm);
	}
}

void plock_exit(void)
{
	saCkptFinalize(system_ckpt_handle);
}

/* locks still marked SYNCING should not go into the ckpt; the new node
   will get those locks by receiving PLOCK_SYNC messages */

static void pack_section_buf(struct lockspace *ls, struct resource *r)
{
	struct pack_plock *pp;
	struct posix_lock *po;
	struct lock_waiter *w;
	int count = 0;

	/* plocks on owned resources are not replicated on other nodes */
	if (r->owner == our_nodeid)
		return;

	pp = (struct pack_plock *) &section_buf;

	list_for_each_entry(po, &r->locks, list) {
		if (po->flags & P_SYNCING)
			continue;
		pp->start	= cpu_to_le64(po->start);
		pp->end		= cpu_to_le64(po->end);
		pp->owner	= cpu_to_le64(po->owner);
		pp->pid		= cpu_to_le32(po->pid);
		pp->nodeid	= cpu_to_le32(po->nodeid);
		pp->ex		= po->ex;
		pp->waiter	= 0;
		pp++;
		count++;
	}

	list_for_each_entry(w, &r->waiters, list) {
		if (w->flags & P_SYNCING)
			continue;
		pp->start	= cpu_to_le64(w->info.start);
		pp->end		= cpu_to_le64(w->info.end);
		pp->owner	= cpu_to_le64(w->info.owner);
		pp->pid		= cpu_to_le32(w->info.pid);
		pp->nodeid	= cpu_to_le32(w->info.nodeid);
		pp->ex		= w->info.ex;
		pp->waiter	= 1;
		pp++;
		count++;
	}

	section_len = count * sizeof(struct pack_plock);
}

static int unpack_section_buf(struct lockspace *ls, char *numbuf, int buflen)
{
	struct pack_plock *pp;
	struct posix_lock *po;
	struct lock_waiter *w;
	struct resource *r;
	int count = section_len / sizeof(struct pack_plock);
	int i, owner = 0;
	unsigned long long num;
	struct timeval now;

	gettimeofday(&now, NULL);

	r = malloc(sizeof(struct resource));
	if (!r)
		return -ENOMEM;
	memset(r, 0, sizeof(struct resource));
	INIT_LIST_HEAD(&r->locks);
	INIT_LIST_HEAD(&r->waiters);
	INIT_LIST_HEAD(&r->pending);

	sscanf(numbuf, "r%llu.%d", &num, &owner);

	r->number = num;
	r->owner = owner;
	r->last_access = now;

	pp = (struct pack_plock *) &section_buf;

	for (i = 0; i < count; i++) {
		if (!pp->waiter) {
			po = malloc(sizeof(struct posix_lock));
			po->start	= le64_to_cpu(pp->start);
			po->end		= le64_to_cpu(pp->end);
			po->owner	= le64_to_cpu(pp->owner);
			po->pid		= le32_to_cpu(pp->pid);
			po->nodeid	= le32_to_cpu(pp->nodeid);
			po->ex		= pp->ex;
			list_add_tail(&po->list, &r->locks);
		} else {
			w = malloc(sizeof(struct lock_waiter));
			w->info.start	= le64_to_cpu(pp->start);
			w->info.end	= le64_to_cpu(pp->end);
			w->info.owner	= le64_to_cpu(pp->owner);
			w->info.pid	= le32_to_cpu(pp->pid);
			w->info.nodeid	= le32_to_cpu(pp->nodeid);
			w->info.ex	= pp->ex;
			list_add_tail(&w->list, &r->waiters);
		}
		pp++;
	}

	list_add_tail(&r->list, &ls->plock_resources);
	return 0;
}

/* If we are the new ckpt_node, we'll be unlinking a ckpt that we don't
   have open, which was created by the previous ckpt_node.  The previous
   ckpt_node should have closed the ckpt in set_plock_ckpt_node() so it
   will go away when we unlink it here. */

static int _unlink_checkpoint(struct lockspace *ls, SaNameT *name)
{
	SaCkptCheckpointHandleT h;
	SaCkptCheckpointDescriptorT s;
	SaAisErrorT rv;
	int ret = 0;

	h = (SaCkptCheckpointHandleT) ls->plock_ckpt_handle;
	log_group(ls, "unlink ckpt %llx", (unsigned long long)h);

 unlink_retry:
	rv = saCkptCheckpointUnlink(system_ckpt_handle, name);
	if (rv == SA_AIS_ERR_TRY_AGAIN) {
		log_group(ls, "unlink ckpt retry");
		sleep(1);
		goto unlink_retry;
	}
	if (rv == SA_AIS_OK)
		goto out_close;

	log_group(ls, "unlink ckpt error %d %s", rv, ls->name);
	ret = -1;

 status_retry:
	rv = saCkptCheckpointStatusGet(h, &s);
	if (rv == SA_AIS_ERR_TRY_AGAIN) {
		log_group(ls, "unlink ckpt status retry");
		sleep(1);
		goto status_retry;
	}
	if (rv != SA_AIS_OK) {
		log_error("unlink ckpt status error %d %s", rv, ls->name);
		goto out_close;
	}

	log_group(ls, "unlink ckpt status: size %llu, max sections %u, "
		      "max section size %llu, section count %u, mem %u",
		 (unsigned long long)s.checkpointCreationAttributes.checkpointSize,
		 s.checkpointCreationAttributes.maxSections,
		 (unsigned long long)s.checkpointCreationAttributes.maxSectionSize,
		 s.numberOfSections, s.memoryUsed);

 out_close:
	if (!h)
		goto out;

	rv = saCkptCheckpointClose(h);
	if (rv == SA_AIS_ERR_TRY_AGAIN) {
		log_group(ls, "unlink ckpt close retry");
		sleep(1);
		goto out_close;
	}
	if (rv != SA_AIS_OK) {
		log_error("unlink ckpt %llx close err %d %s",
			  (unsigned long long)h, rv, ls->name);
		/* should we return an error here and possibly cause
		   store_plocks() to fail on this? */
		/* ret = -1; */
	}
 out:
	ls->plock_ckpt_handle = 0;
	return ret;
}

void close_plock_checkpoint(struct lockspace *ls)
{
	SaCkptCheckpointHandleT h;
	SaAisErrorT rv;

	h = (SaCkptCheckpointHandleT) ls->plock_ckpt_handle;
	if (!h)
		return;
 retry:
	rv = saCkptCheckpointClose(h);
	if (rv == SA_AIS_ERR_TRY_AGAIN) {
		log_group(ls, "close_plock_checkpoint retry");
		sleep(1);
		goto retry;
	}
	if (rv != SA_AIS_OK) {
		log_error("close_plock_checkpoint %llx err %d %s",
			  (unsigned long long)h, rv, ls->name);
	}

	ls->plock_ckpt_handle = 0;
}

/*
 * section id is r<inodenum>.<owner>, the maximum string length is:
 * "r" prefix       =  1    strlen("r")
 * max uint64       = 20    strlen("18446744073709551615")
 * "." before owner =  1    strlen(".")
 * max int          = 11    strlen("-2147483647")
 * \0 at end        =  1
 * ---------------------
 *                    34    SECTION_NAME_LEN
 */

#define SECTION_NAME_LEN 34

/* Copy all plock state into a checkpoint so new node can retrieve it.  The
   node creating the ckpt for the mounter needs to be the same node that's
   sending the mounter its journals message (i.e. the low nodeid).  The new
   mounter knows the ckpt is ready to read only after it gets its journals
   message.
 
   If the mounter is becoming the new low nodeid in the group, the node doing
   the store closes the ckpt and the new node unlinks the ckpt after reading
   it.  The ckpt should then disappear and the new node can create a new ckpt
   for the next mounter. */

void store_plocks(struct lockspace *ls)
{
	SaCkptCheckpointCreationAttributesT attr;
	SaCkptCheckpointHandleT h;
	SaCkptSectionIdT section_id;
	SaCkptSectionCreationAttributesT section_attr;
	SaCkptCheckpointOpenFlagsT flags;
	SaNameT name;
	SaAisErrorT rv;
	char buf[SECTION_NAME_LEN];
	struct resource *r;
	struct posix_lock *po;
	struct lock_waiter *w;
	int r_count, lock_count, total_size, section_size, max_section_size;
	int len, owner;

	if (!cfgd_enable_plock)
		return;

	/* no change to plock state since we created the last checkpoint */
	if (ls->last_checkpoint_time > ls->last_plock_time) {
		log_group(ls, "store_plocks: saved ckpt uptodate");
		goto out;
	}
	ls->last_checkpoint_time = time(NULL);

	len = snprintf((char *)name.value, SA_MAX_NAME_LENGTH, "dlmplock.%s",
		       ls->name);
	name.length = len;

	_unlink_checkpoint(ls, &name);

	/* loop through all plocks to figure out sizes to set in
	   the attr fields */

	r_count = 0;
	lock_count = 0;
	total_size = 0;
	max_section_size = 0;

	list_for_each_entry(r, &ls->plock_resources, list) {
		if (r->owner == -1)
			continue;

		r_count++;
		section_size = 0;
		list_for_each_entry(po, &r->locks, list) {
			section_size += sizeof(struct pack_plock);
			lock_count++;
		}
		list_for_each_entry(w, &r->waiters, list) {
			section_size += sizeof(struct pack_plock);
			lock_count++;
		}
		total_size += section_size;
		if (section_size > max_section_size)
			max_section_size = section_size;
	}

	log_group(ls, "store_plocks: r_count %d, lock_count %d, pp %u bytes",
		  r_count, lock_count, (unsigned int)sizeof(struct pack_plock));

	log_group(ls, "store_plocks: total %d bytes, max_section %d bytes",
		  total_size, max_section_size);

	attr.creationFlags = SA_CKPT_WR_ALL_REPLICAS;
	attr.checkpointSize = total_size;
	attr.retentionDuration = SA_TIME_MAX;
	attr.maxSections = r_count + 1;      /* don't know why we need +1 */
	attr.maxSectionSize = max_section_size;
	attr.maxSectionIdSize = SECTION_NAME_LEN;

	flags = SA_CKPT_CHECKPOINT_READ |
		SA_CKPT_CHECKPOINT_WRITE |
		SA_CKPT_CHECKPOINT_CREATE;

 open_retry:
	rv = saCkptCheckpointOpen(system_ckpt_handle, &name,&attr,flags,0,&h);
	if (rv == SA_AIS_ERR_TRY_AGAIN) {
		log_group(ls, "store_plocks: ckpt open retry");
		sleep(1);
		goto open_retry;
	}
	if (rv == SA_AIS_ERR_EXIST) {
		log_group(ls, "store_plocks: ckpt already exists");
		return;
	}
	if (rv != SA_AIS_OK) {
		log_error("store_plocks: ckpt open error %d %s", rv, ls->name);
		return;
	}

	log_group(ls, "store_plocks: open ckpt handle %llx",
		  (unsigned long long)h);
	ls->plock_ckpt_handle = (uint64_t) h;

	/* - If r owner is -1, ckpt nothing.
	   - If r owner is us, ckpt owner of us and no plocks.
	   - If r owner is other, ckpt that owner and any plocks we have on r
	     (they've just been synced but owner=0 msg not recved yet).
	   - If r owner is 0 and !got_unown, then we've just unowned r;
	     ckpt owner of us and any plocks that don't have SYNCING set
	     (plocks with SYNCING will be handled by our sync messages).
	   - If r owner is 0 and got_unown, then ckpt owner 0 and all plocks;
	     (there should be no SYNCING plocks) */

	list_for_each_entry(r, &ls->plock_resources, list) {
		if (r->owner == -1)
			continue;
		else if (r->owner == our_nodeid)
			owner = our_nodeid;
		else if (r->owner)
			owner = r->owner;
		else if (!r->owner && !got_unown(r))
			owner = our_nodeid;
		else if (!r->owner)
			owner = 0;
		else {
			log_error("store_plocks owner %d r %llx", r->owner,
				  (unsigned long long)r->number);
			continue;
		}

		memset(&buf, 0, sizeof(buf));
		len = snprintf(buf, SECTION_NAME_LEN, "r%llu.%d",
			       (unsigned long long)r->number, owner);

		section_id.id = (void *)buf;
		section_id.idLen = len + 1;
		section_attr.sectionId = &section_id;
		section_attr.expirationTime = SA_TIME_END;

		memset(&section_buf, 0, sizeof(section_buf));
		section_len = 0;

		pack_section_buf(ls, r);

		log_group(ls, "store_plocks: section size %u id %u \"%s\"",
			  section_len, section_id.idLen, buf);

	 create_retry:
		rv = saCkptSectionCreate(h, &section_attr, &section_buf,
					 section_len);
		if (rv == SA_AIS_ERR_TRY_AGAIN) {
			log_group(ls, "store_plocks: ckpt create retry");
			sleep(1);
			goto create_retry;
		}
		if (rv == SA_AIS_ERR_EXIST) {
			/* this shouldn't happen in general */
			log_group(ls, "store_plocks: clearing old ckpt");
			/* do we need this close or will the close in
			   the unlink function be ok? */
			saCkptCheckpointClose(h);
			_unlink_checkpoint(ls, &name);
			goto open_retry;
		}
		if (rv != SA_AIS_OK) {
			log_error("store_plocks: ckpt section create err %d %s",
				  rv, ls->name);
			break;
		}
	}
 out:
	return;
}

/* called by a node that's just been added to the group to get existing plock
   state */

void retrieve_plocks(struct lockspace *ls)
{
	SaCkptCheckpointHandleT h;
	SaCkptSectionIterationHandleT itr;
	SaCkptSectionDescriptorT desc;
	SaCkptIOVectorElementT iov;
	SaNameT name;
	SaAisErrorT rv;
	char buf[SECTION_NAME_LEN];
	int len;

	if (!cfgd_enable_plock)
		return;

	log_group(ls, "retrieve_plocks");

	len = snprintf((char *)name.value, SA_MAX_NAME_LENGTH, "dlmplock.%s",
		       ls->name);
	name.length = len;

 open_retry:
	rv = saCkptCheckpointOpen(system_ckpt_handle, &name, NULL,
				  SA_CKPT_CHECKPOINT_READ, 0, &h);
	if (rv == SA_AIS_ERR_TRY_AGAIN) {
		log_group(ls, "retrieve_plocks: ckpt open retry");
		sleep(1);
		goto open_retry;
	}
	if (rv != SA_AIS_OK) {
		log_error("retrieve_plocks: ckpt open error %d %s",
			  rv, ls->name);
		return;
	}

 init_retry:
	rv = saCkptSectionIterationInitialize(h, SA_CKPT_SECTIONS_ANY, 0, &itr);
	if (rv == SA_AIS_ERR_TRY_AGAIN) {
		log_group(ls, "retrieve_plocks: ckpt iterinit retry");
		sleep(1);
		goto init_retry;
	}
	if (rv != SA_AIS_OK) {
		log_error("retrieve_plocks: ckpt iterinit error %d %s",
			  rv, ls->name);
		goto out;
	}

	while (1) {
	 next_retry:
		rv = saCkptSectionIterationNext(itr, &desc);
		if (rv == SA_AIS_ERR_NO_SECTIONS)
			break;
		if (rv == SA_AIS_ERR_TRY_AGAIN) {
			log_group(ls, "retrieve_plocks: ckpt iternext retry");
			sleep(1);
			goto next_retry;
		}
		if (rv != SA_AIS_OK) {
			log_error("retrieve_plocks: ckpt iternext error %d %s",
				  rv, ls->name);
			goto out_it;
		}

		if (!desc.sectionSize)
			continue;

		iov.sectionId = desc.sectionId;
		iov.dataBuffer = &section_buf;
		iov.dataSize = desc.sectionSize;
		iov.dataOffset = 0;

		memset(&buf, 0, sizeof(buf));
		snprintf(buf, SECTION_NAME_LEN, "%s", desc.sectionId.id);
		log_group(ls, "retrieve_plocks: section size %llu id %u \"%s\"",
			  (unsigned long long)iov.dataSize, iov.sectionId.idLen,
			  buf);

	 read_retry:
		rv = saCkptCheckpointRead(h, &iov, 1, NULL);
		if (rv == SA_AIS_ERR_TRY_AGAIN) {
			log_group(ls, "retrieve_plocks: ckpt read retry");
			sleep(1);
			goto read_retry;
		}
		if (rv != SA_AIS_OK) {
			log_error("retrieve_plocks: ckpt read error %d %s",
				  rv, ls->name);
			goto out_it;
		}

		log_group(ls, "retrieve_plocks: ckpt read %llu bytes",
			  (unsigned long long)iov.readSize);
		section_len = iov.readSize;

		if (!section_len)
		       continue;

		if (section_len % sizeof(struct pack_plock)) {
			log_error("retrieve_plocks: bad section len %d %s",
				  section_len, ls->name);
			continue;
		}

		unpack_section_buf(ls, (char *)desc.sectionId.id,
				   desc.sectionId.idLen);
	}

 out_it:
	saCkptSectionIterationFinalize(itr);
 out:
	saCkptCheckpointClose(h);
}

/* Called when a node has failed, or we're unmounting.  For a node failure, we
   need to call this when the cpg confchg arrives so that we're guaranteed all
   nodes do this in the same sequence wrt other messages. */

void purge_plocks(struct lockspace *ls, int nodeid, int unmount)
{
	struct posix_lock *po, *po2;
	struct lock_waiter *w, *w2;
	struct resource *r, *r2;
	int purged = 0;

	if (!cfgd_enable_plock)
		return;

	list_for_each_entry_safe(r, r2, &ls->plock_resources, list) {
		list_for_each_entry_safe(po, po2, &r->locks, list) {
			if (po->nodeid == nodeid || unmount) {
				list_del(&po->list);
				free(po);
				purged++;
			}
		}

		list_for_each_entry_safe(w, w2, &r->waiters, list) {
			if (w->info.nodeid == nodeid || unmount) {
				list_del(&w->list);
				free(w);
				purged++;
			}
		}

		/* TODO: haven't thought carefully about how this transition
		   to owner 0 might interact with other owner messages in
		   progress. */

		if (r->owner == nodeid) {
			r->owner = 0;
			send_pending_plocks(ls, r);
		}
		
		if (!list_empty(&r->waiters))
			do_waiters(ls, r);

		if (!cfgd_plock_ownership &&
		    list_empty(&r->locks) && list_empty(&r->waiters)) {
			list_del(&r->list);
			free(r);
		}
	}
	
	if (purged)
		ls->last_plock_time = time(NULL);

	log_group(ls, "purged %d plocks for %d", purged, nodeid);
}

int fill_plock_dump_buf(struct lockspace *ls)
{
	struct posix_lock *po;
	struct lock_waiter *w;
	struct resource *r;
	int rv = 0;
	int len = DLMC_DUMP_SIZE, pos = 0, ret;

	memset(plock_dump_buf, 0, sizeof(plock_dump_buf));
	plock_dump_len = 0;

	list_for_each_entry(r, &ls->plock_resources, list) {
		list_for_each_entry(po, &r->locks, list) {
			ret = snprintf(plock_dump_buf + pos, len - pos,
			      "%llu %s %llu-%llu nodeid %d pid %u owner %llx\n",
			      (unsigned long long)r->number,
			      po->ex ? "WR" : "RD",
			      (unsigned long long)po->start,
			      (unsigned long long)po->end,
			      po->nodeid, po->pid,
			      (unsigned long long)po->owner);

			if (ret >= len - pos) {
				rv = -ENOSPC;
				goto out;
			}
			pos += ret;
		}

		list_for_each_entry(w, &r->waiters, list) {
			ret = snprintf(plock_dump_buf + pos, len - pos,
			      "%llu WAITING %s %llu-%llu nodeid %d pid %u owner %llx\n",
			      (unsigned long long)r->number,
			      w->info.ex ? "WR" : "RD",
			      (unsigned long long)w->info.start,
			      (unsigned long long)w->info.end,
			      w->info.nodeid, w->info.pid,
			      (unsigned long long)w->info.owner);

			if (ret >= len - pos) {
				rv = -ENOSPC;
				goto out;
			}
			pos += ret;
		}
	}
 out:
	return rv;
}

#if 0
/* replace all of the above with this to build on 2.6.25 kernels */

int setup_plocks(void) { return 0; };
void process_plocks(int ci) { };
int limit_plocks(void) { return 0; };
void receive_plock(struct lockspace *ls, struct dlm_header *hd, int len) { };
void receive_own(struct lockspace *ls, struct dlm_header *hd, int len) { };
void receive_sync(struct lockspace *ls, struct dlm_header *hd, int len) { };
void receive_drop(struct lockspace *ls, struct dlm_header *hd, int len) { };
void process_saved_plocks(struct lockspace *ls) { };
void close_plock_checkpoint(struct lockspace *ls) { };
void store_plocks(struct lockspace *ls) { };
void retrieve_plocks(struct lockspace *ls) { };
void purge_plocks(struct lockspace *ls, int nodeid, int unmount) { };
int dump_plocks(char *name, int fd) { return 0; };

#endif

