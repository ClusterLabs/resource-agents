/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2007 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <syslog.h>
#include <asm/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/errno.h>

#include "libdlm.h"

#define MAX_CLIENTS 4
#define MAX_LOCKS 16
#define MAX_RESOURCES 16

static dlm_lshandle_t *dh;
static int libdlm_fd;
static int timewarn = 0;
static uint64_t timeout = 0;
static int noqueue = 1;
static int persistent = 0;
static int ignore_bast = 0;
static int quiet = 1;
static int verbose = 0;
static int bast_cb;
static int maxn = MAX_LOCKS;
static int maxr = MAX_RESOURCES;
static int iterations;
static int minhold = 0;
static int stress_stop = 0;
static int stress_delay = 0;
static int stress_lock_only = 0;
static int openclose_ls = 0;
static uint64_t our_xid;

static unsigned int sts_eunlock, sts_ecancel, sts_etimedout, sts_edeadlk, sts_eagain, sts_other, sts_zero;
static unsigned int bast_unlock, bast_skip;

#define log_print(fmt, args...) \
do { \
	if (!quiet) \
		printf(fmt , ##args); \
} while (0)

#define log_verbose(fmt, args...) \
do { \
	if (verbose) \
		printf(fmt , ##args); \
} while (0)

struct client {
	int fd;
	char type[32];
};

static int client_size = MAX_CLIENTS;
static struct client client[MAX_CLIENTS];
static struct pollfd pollfd[MAX_CLIENTS];

enum {
	Op_lock = 1,
	Op_unlock,
	Op_unlockf,
	Op_cancel,
};

struct lk {
	int id;
	int rqmode;
	int grmode;
	int wait_ast;
	int lastop;
	int last_status;
	int bast;
	int minhold;
	struct dlm_lksb lksb;
	struct timeval begin;
	struct timeval acquired;
};

struct lk *locks;

void unlock(int i);
void unlockf(int i);


int rand_int(int a, int b)
{
	return a + (int) (((float)(b - a + 1)) * random() / (RAND_MAX+1.0)); 
}

char *status_str(int status)
{
	static char sts_str[8];

	switch (status) {
	case 0:
		return "0      ";
	case EUNLOCK:
		return "EUNLOCK";
	case ECANCEL:
		return "ECANCEL";
	case EAGAIN:
		return "EAGAIN ";
	case EBUSY:
		return "EBUSY  ";
	case ETIMEDOUT:
		return "ETIMEDO";
	case EDEADLK:
		return "EDEADLK";
	default:
		snprintf(sts_str, 8, "%8x", status);
		return sts_str;
	}
}

char *op_str(int op)
{
	switch (op) {
	case Op_lock:
		return "lock";
	case Op_unlock:
		return "unlock";
	case Op_unlockf:
		return "unlockf";
	case Op_cancel:
		return "cancel";
	default:
		return "unknown";
	}
}

struct lk *get_lock(int i)
{
	if (i < 0)
		return NULL;
	if (i >= maxn)
		return NULL;
	return &locks[i];
}

int all_unlocks_done(void)
{
	struct lk *lk;
	int i;

	for (i = 0; i < maxn; i++) {
		lk = get_lock(i);
		if (lk->grmode == -1 && !lk->wait_ast)
			continue;
		return 0;
	}
	return 1;
}

void dump(void)
{
	struct timeval now;
	struct lk *lk;
	int i;

	gettimeofday(&now, NULL);

	for (i = 0; i < maxn; i++) {
		lk = get_lock(i);
		printf("x %2d lkid %08x gr %2d rq %2d wait_ast %d last op %s  \t%s  %us\n",
			i,
			lk->lksb.sb_lkid,
			lk->grmode,
			lk->rqmode,
			lk->wait_ast,
			op_str(lk->lastop),
			status_str(lk->last_status),
			lk->wait_ast ? now.tv_sec - lk->begin.tv_sec : 0);
	}
}

void bastfn(void *arg)
{
	struct lk *lk = arg;
	lk->bast = 1;
	bast_cb = 1;
}

void do_bast(struct lk *lk)
{
	int skip = 0;

	if (lk->lastop == Op_unlock || lk->lastop == Op_unlockf) {
		skip = 1;
	}
	if (!lk->lksb.sb_lkid) {
		skip = 1;
	}

	if (skip) {
		bast_skip++;
		printf("    bast: skip    %3d\t%x\n", lk->id, lk->lksb.sb_lkid);
	} else {
		bast_unlock++;
		printf("    bast: unlockf %3d\t%x\n", lk->id, lk->lksb.sb_lkid);
		unlockf(lk->id);
	}
	lk->bast = 0;
}

void do_bast_unlocks(void)
{
	struct lk *lk;
	int i;

	for (i = 0; i < maxn; i++) {
		lk = get_lock(i);
		if (lk->bast)
			do_bast(lk);
	}
	bast_cb = 0;
}

void process_libdlm(void)
{
	dlm_dispatch(libdlm_fd);
	if (bast_cb && !ignore_bast)
		do_bast_unlocks();
}

void astfn(void *arg)
{
	struct lk *lk = arg;
	int i = lk->id;

	if (!lk->wait_ast) {
		printf("     ast: %s %3d\t%x: !wait_ast gr %d rq %d last op %s %s\n",
		       status_str(lk->lksb.sb_status), i, lk->lksb.sb_lkid,
		       lk->grmode, lk->rqmode,
		       op_str(lk->lastop), status_str(lk->last_status));
	}

	log_print("     ast: %s %3d\t%x\n",
		  status_str(lk->lksb.sb_status), i, lk->lksb.sb_lkid);

	lk->last_status = lk->lksb.sb_status;

	if (lk->lksb.sb_status == EUNLOCK) {
		sts_eunlock++;
		memset(&lk->lksb, 0, sizeof(struct dlm_lksb));
		lk->grmode = -1;
		lk->wait_ast = 0;

	} else if (lk->lksb.sb_status == ECANCEL) {
		sts_ecancel++;
		if (lk->grmode == -1) {
			memset(&lk->lksb, 0, sizeof(struct dlm_lksb));
			lk->wait_ast = 0;
		} else {
			if (lk->lastop != Op_unlock && lk->lastop != Op_unlockf)
				lk->wait_ast = 0;
		}

	} else if (lk->lksb.sb_status == ETIMEDOUT) {
		sts_etimedout++;
		if (lk->grmode == -1) {
			memset(&lk->lksb, 0, sizeof(struct dlm_lksb));
			lk->wait_ast = 0;
		} else {
			if (lk->lastop != Op_unlock && lk->lastop != Op_unlockf)
				lk->wait_ast = 0;
		}

	} else if (lk->lksb.sb_status == EDEADLK) {
		sts_edeadlk++;
		if (lk->grmode == -1) {
			memset(&lk->lksb, 0, sizeof(struct dlm_lksb));
			lk->wait_ast = 0;
		} else {
			if (lk->lastop != Op_unlock && lk->lastop != Op_unlockf)
				lk->wait_ast = 0;
		}

	} else if (lk->lksb.sb_status == EAGAIN) {
		sts_eagain++;
		if (lk->grmode == -1) {
			memset(&lk->lksb, 0, sizeof(struct dlm_lksb));
			lk->wait_ast = 0;
		} else {
			if (lk->lastop != Op_unlockf)
				lk->wait_ast = 0;
		}

	} else {
		if (lk->lksb.sb_status != 0) {
			sts_other++;
			printf("BAD  ast: %d %3d\t%x: gr %d rq %d last op %s %s\n",
		       		lk->lksb.sb_status, i, lk->lksb.sb_lkid,
				lk->grmode, lk->rqmode, op_str(lk->lastop),
				status_str(lk->last_status));
			stress_stop = 1;
			return;
		}

		sts_zero++;

		if (lk->lastop != Op_unlockf)
			lk->wait_ast = 0;

		lk->grmode = lk->rqmode;

		if (minhold) {
			gettimeofday(&lk->acquired, NULL);
			lk->minhold = minhold;
		}
	}

	lk->rqmode = -1;
}

/* EBUSY from dlm_ls_lockx() is expected sometimes, e.g. lock, cancel, lock;
   the first lock is successful and the app gets the status back,
   and issues the second lock before the reply for the overlapping
   cancel (which did nothing) has been received in the dlm. */

void lock(int i, int mode)
{
	char name[DLM_RESNAME_MAXLEN];
	struct lk *lk;
	int flags = 0;
	int rv;
	uint64_t *timeout_arg = NULL;

	lk = get_lock(i);
	if (!lk)
		return;

	if (noqueue)
		flags |= LKF_NOQUEUE;
	if (persistent)
		flags |= LKF_PERSISTENT;
	if (timeout) {
		flags |= LKF_TIMEOUT;
		timeout_arg = &timeout;
	}

	if (lk->lksb.sb_lkid)
		flags |= LKF_CONVERT;

	memset(name, 0, sizeof(name));
	snprintf(name, sizeof(name), "test%d", (i % maxr));

	log_verbose("lock: %d grmode %d rqmode %d flags %x lkid %x %s\n",
	            i, lk->grmode, mode, flags, lk->lksb.sb_lkid, name);

#if 0
	rv = dlm_ls_lock(dh, mode, &lk->lksb, flags, name, strlen(name), 0,
			  astfn, (void *) lk, bastfn, NULL);
#else
	rv = dlm_ls_lockx(dh, mode, &lk->lksb, flags, name, strlen(name), 0,
			  astfn, (void *) lk, bastfn, &our_xid, timeout_arg);
#endif
	if (!rv) {
		lk->wait_ast = 1;
		lk->rqmode = mode;
		gettimeofday(&lk->begin, NULL);
	} else if (rv == -1 && errno == EBUSY) {
		printf("        : lock    %3d\t%x: EBUSY gr %d rq %d wait_ast %d\n",
			i, lk->lksb.sb_lkid, lk->grmode, lk->rqmode, lk->wait_ast);
	} else {
		printf("        : lock    %3d\t%x: errno %d rv %d gr %d rq %d wait_ast %d\n",
			i, lk->lksb.sb_lkid, errno, rv, lk->grmode, lk->rqmode, lk->wait_ast);
		stress_stop = 1;
	}

	log_verbose("lock: %d rv %d sb_lkid %x sb_status %x\n",
	            i, rv, lk->lksb.sb_lkid, lk->lksb.sb_status);

	lk->lastop = Op_lock;
}

void lock_sync(int i, int mode)
{
	char name[DLM_RESNAME_MAXLEN];
	int flags = 0;
	int rv;
	struct lk *lk;

	lk = get_lock(i);
	if (!lk)
		return;

	if (noqueue)
		flags |= LKF_NOQUEUE;
	if (persistent)
		flags |= LKF_PERSISTENT;

	if (lk->lksb.sb_lkid)
		flags |= LKF_CONVERT;

	memset(name, 0, sizeof(name));
	snprintf(name, sizeof(name), "test%d", (i % maxr));

	log_verbose("lock_sync: %d rqmode %d flags %x lkid %x %s\n",
	            i, mode, flags, lk->lksb.sb_lkid, name);

	rv = dlm_ls_lock_wait(dh, mode, &lk->lksb, flags,
			 name, strlen(name), 0, (void *) lk,
			 bastfn, NULL);

	log_verbose("lock_sync: %d rv %d sb_lkid %x sb_status %x\n",
	            i, rv, lk->lksb.sb_lkid, lk->lksb.sb_status);

	if (!rv) {
		lk->grmode = mode;
		lk->rqmode = -1;
	} else if (rv == EAGAIN) {
		if (lk->grmode == -1)
			memset(&lk->lksb, 0, sizeof(struct dlm_lksb));
	} else {
		printf("unknown rv %d\n", rv);
		exit(-1);
	}
}

void lock_all(int mode)
{
	int i;

	for (i = 0; i < maxn; i++)
		lock(i, mode);
}

char *uflags(uint32_t flags)
{
	if (flags == LKF_FORCEUNLOCK)
		return "FORCEUNLOCK";
	if (flags == LKF_CANCEL)
		return "CANCEL";
	return "0";
}

/* ENOENT is expected from dlm_ls_unlock() sometimes because we'll
   try to do an unlockf during an outstanding op that will free
   the lock itself */

void _unlock(int i, uint32_t flags)
{
	struct lk *lk;
	uint32_t lkid;
	int rv;

	lk = get_lock(i);
	if (!lk)
		return;

	lkid = lk->lksb.sb_lkid;
	if (!lkid)
		return;

	log_verbose("unlock: %d lkid %x flags %x\n", i, lkid, flags);

	rv = dlm_ls_unlock(dh, lkid, flags, &lk->lksb, lk);
	if (!rv) {
		lk->wait_ast = 1;
		gettimeofday(&lk->begin, NULL);
	} else if (rv == -1 && errno == EBUSY) {
		printf("        : unlock  %3d\t%x: EBUSY flags %s gr %d rq %d wait_ast %d\n",
			i, lk->lksb.sb_lkid, uflags(flags), lk->grmode, lk->rqmode, lk->wait_ast);
	} else if (rv == -1 && errno == ENOENT) {
		printf("        : unlock  %3d\t%x: ENOENT flags %s gr %d rq %d wait_ast %d\n",
			i, lk->lksb.sb_lkid, uflags(flags), lk->grmode, lk->rqmode, lk->wait_ast);
	} else {
		printf("        : unlock  %3d\t%x: errno %d flags %s rv %d gr %d rq %d wait_ast %d\n",
			i, lk->lksb.sb_lkid, errno, uflags(flags), rv, lk->grmode, lk->rqmode, lk->wait_ast);
	}
}

void unlock(int i)
{
	struct lk *lk = get_lock(i);

	if (minhold) {
		struct timeval now;

		if (lk->wait_ast)
			return;

		gettimeofday(&now, NULL);
		if (lk->acquired.tv_sec + lk->minhold > now.tv_sec) {
			printf("        : unlock  %3d\t%x: gr %d rq %d held %u of %u s\n",
				i, lk->lksb.sb_lkid, lk->grmode, lk->rqmode,
				now.tv_sec - lk->acquired.tv_sec, lk->minhold);
			return;
		}
	}

	_unlock(i, 0);
	lk->rqmode = -1;
	lk->lastop = Op_unlock;
}

void unlockf(int i)
{
	struct lk *lk = get_lock(i);

	if (minhold) {
		struct timeval now;

		if (lk->wait_ast)
			return;

		gettimeofday(&now, NULL);
		if (lk->acquired.tv_sec + lk->minhold > now.tv_sec) {
			printf("        : unlockf %3d\t%x: gr %d rq %d held %u of %u s\n",
				i, lk->lksb.sb_lkid, lk->grmode, lk->rqmode,
				now.tv_sec - lk->acquired.tv_sec, lk->minhold);
			return;
		}
	}

	_unlock(i, LKF_FORCEUNLOCK);
	lk->rqmode = -1;
	lk->lastop = Op_unlockf;
}

void cancel(int i)
{
	struct lk *lk = get_lock(i);
	_unlock(i, LKF_CANCEL);
	lk->lastop = Op_cancel;
}

void canceld(int i, uint32_t lkid)
{
	int rv;

	rv = dlm_ls_deadlock_cancel(dh, lkid, 0);

	printf("canceld %x: %d %d\n", lkid, rv, errno);
}

void unlock_sync(int i)
{
	uint32_t lkid;
	int rv;
	struct lk *lk;

	lk = get_lock(i);
	if (!lk)
		return;

	lkid = lk->lksb.sb_lkid;
	if (!lkid) {
		log_print("unlock %d skip zero lkid\n", i);
		return;
	}

	log_verbose("unlock_sync: %d lkid %x\n", i, lkid);

	rv = dlm_ls_unlock_wait(dh, lkid, 0, &lk->lksb);

	log_verbose("unlock_sync: %d rv %d sb_status %x\n", i, rv,
	            lk->lksb.sb_status);

	memset(&lk->lksb, 0, sizeof(struct dlm_lksb));
	lk->grmode = -1;
	lk->rqmode = -1;
}

void unlock_all(void)
{
	struct lk *lk;
	int i;

	for (i = 0; i < maxn; i++) {
		lk = get_lock(i);
		unlock(i);
	}
}

void purge(int nodeid, int pid)
{
	struct lk *lk;
	int i, rv;

	rv = dlm_ls_purge(dh, nodeid, pid);
	if (rv) {
		printf("dlm_ls_purge %d %d error %d\n", nodeid, pid, rv);
		return;
	}

	for (i = 0; i < maxn; i++) {
		lk = get_lock(i);
		memset(&lk->lksb, 0, sizeof(struct dlm_lksb));
		lk->grmode = -1;
		lk->wait_ast = 0;
	}
}

void purgetest(int nodeid, int pid)
{
	struct lk *lk;
	int i, mid = maxn / 2;

	printf("lock %d to %d\n", 0, mid-1);
	for (i = 0; i < mid; i++)
		lock(i, 3);

	while (1) {
		process_libdlm();
		for (i = 0; i < mid; i++) {
			lk = get_lock(i);
			if (!lk->wait_ast)
				continue;
			break;
		}
		if (i == mid)
			break;
	}

	for (i = mid; i < maxn; i++)
		lock(i, 3);
	for (i = 0; i < mid; i++)
		unlock(i);
	/* usleep(10000); */
	purge(nodeid, pid);
}

void tstress_unlocks(void)
{
	struct lk *lk;
	struct timeval now;
	int i;

	for (i = 0; i < maxn; i++) {
		lk = get_lock(i);
		if (!lk)
			continue;
		if (lk->wait_ast)
			continue;
		if (lk->grmode < 0)
			continue;

		/* if we've held the lock for minhold seconds, then unlock */

		gettimeofday(&now, NULL);

		if (now.tv_sec >= lk->acquired.tv_sec + minhold) {
			printf("        : unlock  %3d\t%x: gr %d rq %d held %u of %u s\n",
				i, lk->lksb.sb_lkid, lk->grmode, lk->rqmode,
				now.tv_sec - lk->acquired.tv_sec, minhold);

			_unlock(i, 0);
			lk->rqmode = -1;
			lk->lastop = Op_unlock;
		}

	}
}

void tstress(int num)
{
	int i, o, op, max_op, skip;
	unsigned int n, skips, lock_ops, unlock_ops, unlockf_ops, cancel_ops;
	struct lk *lk;

	n = skips = lock_ops = unlock_ops = unlockf_ops = cancel_ops = 0;
	sts_eunlock = sts_ecancel = sts_etimedout = sts_edeadlk = sts_eagain = sts_other = sts_zero = 0;
	bast_unlock = bast_skip = 0;

	noqueue = 0;
	ignore_bast = 1;
	quiet = 0;

	if (!timeout)
		timeout = 4;
	if (!minhold)
		minhold = 5;

	while (!stress_stop) {
		if (stress_delay)
			usleep(stress_delay);

		process_libdlm();

		tstress_unlocks();

		if (++n == num) {
			if (all_unlocks_done())
				break;
			else
				continue;
		}

		i = rand_int(0, maxn-1);
		lk = get_lock(i);
		if (!lk)
			continue;

		if (lk->wait_ast || lk->grmode > -1)
			continue;

		lock(i, rand_int(0, 5));
		lock_ops++;
		printf("%8x: lock    %3d\t%x\n", n, i, lk->lksb.sb_lkid);
	}

	printf("ops: skip %d lock %d unlock %d unlockf %d cancel %d\n",
		skips, lock_ops, unlock_ops, unlockf_ops, cancel_ops);
	printf("bast: unlock %u skip %u\n", bast_unlock, bast_skip);
	printf("ast status: eunlock %d ecancel %d etimedout %d edeadlk %d eagain %d\n",
		sts_eunlock, sts_ecancel, sts_etimedout, sts_edeadlk, sts_eagain);
	printf("ast status: zero %d other %d\n", sts_zero, sts_other);
}

void stress(int num)
{
	int i, o, op, max_op, skip;
	unsigned int n, skips, lock_ops, unlock_ops, unlockf_ops, cancel_ops;
	struct lk *lk;

	n = skips = lock_ops = unlock_ops = unlockf_ops = cancel_ops = 0;
	sts_eunlock = sts_ecancel = sts_etimedout = sts_edeadlk = sts_eagain = sts_other = sts_zero = 0;
	bast_unlock = bast_skip = 0;

	while (!stress_stop) {
		if (stress_delay)
			usleep(stress_delay);

		process_libdlm();

		if (++n == num)
			break;

		i = rand_int(0, maxn-1);
		lk = get_lock(i);
		if (!lk)
			continue;

		max_op = 5;
		if (stress_lock_only)
			max_op = 2;

		o = rand_int(0, max_op);
		switch (o) {
		case 0:
		case 1:
		case 2:
			op = Op_lock;
			break;
		case 3:
			op = Op_unlock;
			break;
		case 4:
			op = Op_unlockf;
			break;
		case 5:
			op = Op_cancel;
			break;
		}

		skip = 0;

		switch (op) {
		case Op_lock:
			if (lk->wait_ast) {
				skip = 1;
				break;
			}

			noqueue = !!o;
			our_xid = n;

			lock(i, rand_int(0, 5));
			lock_ops++;
			printf("%8x: lock    %3d\t%x\n", n, i, lk->lksb.sb_lkid);
			break;

		case Op_unlock:
			if (lk->wait_ast) {
				skip = 1;
				break;
			}
			if (lk->lastop == Op_unlock || lk->lastop == Op_unlockf) {
				skip = 1;
				break;
			}
			if (!lk->lksb.sb_lkid) {
				skip = 1;
				break;
			}

			unlock(i);
			unlock_ops++;
			printf("%8x: unlock  %3d\t%x\n", n, i, lk->lksb.sb_lkid);
			break;

		case Op_unlockf:
			if (lk->lastop == Op_unlock || lk->lastop == Op_unlockf) {
				skip = 1;
				break;
			}
			if (!lk->lksb.sb_lkid) {
				skip = 1;
				break;
			}

			unlockf(i);
			unlockf_ops++;
			printf("%8x: unlockf %3d\t%x\n", n, i, lk->lksb.sb_lkid);
			break;

		case Op_cancel:
			if (!lk->wait_ast) {
				skip = 1;
				break;
			}
			if (lk->lastop > Op_lock) {
				skip = 1;
				break;
			}

			cancel(i);
			cancel_ops++;
			printf("%8x: cancel  %3d\t%x\n", n, i, lk->lksb.sb_lkid);
			break;
		}

		if (skip)
			skips++;
	}

	printf("ops: skip %d lock %d unlock %d unlockf %d cancel %d\n",
		skips, lock_ops, unlock_ops, unlockf_ops, cancel_ops);
	printf("bast: unlock %u skip %u\n", bast_unlock, bast_skip);
	printf("ast status: eunlock %d ecancel %d etimedout %d edeadlk %d eagain %d\n",
		sts_eunlock, sts_ecancel, sts_etimedout, sts_edeadlk, sts_eagain);
	printf("ast status: zero %d other %d\n", sts_zero, sts_other);
}

static int client_add(int fd, int *maxi)
{
	int i;

	for (i = 0; i < client_size; i++) {
		if (client[i].fd == -1) {
			client[i].fd = fd;
			pollfd[i].fd = fd;
			pollfd[i].events = POLLIN;
			if (i > *maxi)
				*maxi = i;
			printf("client %d fd %d added\n", i, fd);
			return i;
		}
	}
	printf("client add failed\n");
	return -1;
}

static void client_dead(int ci)
{
	printf("client %d fd %d dead\n", ci, client[ci].fd);
	close(client[ci].fd);
	client[ci].fd = -1;
	pollfd[ci].fd = -1;
}

static void client_init(void)
{
	int i;

	for (i = 0; i < client_size; i++)
		client[i].fd = -1;
}

void print_commands(void)
{
	printf("Usage:\n");
	printf("max locks (maxn) is %d (x of 0 to %d)\n", maxn, maxn-1);
	printf("max resources (maxr) is %d, lock x used on resource (x % maxr)\n", maxr);
	printf("EXIT		 - exit program after unlocking any held locks\n");
	printf("kill		 - exit program without unlocking any locks\n");
	printf("lock x mode	 - request/convert lock x\n");
	printf("unlock x 	 - unlock lock x\n");
	printf("unlockf x 	 - force unlock lock x\n");
	printf("cancel x 	 - cancel lock x\n");
	printf("canceld x lkid	 - cancel lock x, return EDEADLK as status\n");
	printf("lock_sync x mode - synchronous version of lock\n");
	printf("unlock_sync x 	 - synchronous version of unlock\n");
	printf("ex x		 - equivalent to: lock x 5\n");
	printf("pr x		 - equivalent to: lock x 3\n");
	printf("hold		 - for x in 0 to max, lock x 3\n");
	printf("release		 - for x in 0 to max, unlock x\n");
	printf("purge nodeid pid - purge orphan locks of process\n");
	printf("stress n	 - loop doing random lock/unlock/unlockf/cancel on all locks, n times\n");
	printf("tstress n	 - stress timeouts\n");
	printf("timeout n	 - enable lock timeouts, set timeout to n seconds\n");
	printf("dump		 - show info for all locks\n");
	printf("minhold		 - set minimum number of seconds locks will be held\n");
	printf("ignore_bast	 - ignore all basts\n");
	printf("noqueue		 - toggle NOQUEUE flag for all requests\n");
	printf("persistent	 - toggle PERSISTENT flag for all requests\n");
	printf("quiet		 - toggle quiet flag\n");
	printf("verbose		 - toggle verbose flag\n");

	printf("\ncombined operations\n");
	printf("hold-kill                     - hold; kill\n");
	printf("release-kill                  - release; kill\n");
	printf("lock-kill x mode              - lock; kill\n");
	printf("unlock-kill x                 - unlock; kill\n");
	printf("lock-cancel x mode msec       - lock; sleep; cancel\n");
	printf("lock-unlockf x mode msec      - lock; sleep; unlockf\n");
	printf("lock-cancel-kill x mode msec  - lock; sleep; cancel; kill\n");
	printf("lock-unlockf-kill x mode msec - lock; sleep; unlockf; kill\n");
	printf("purgetest nodeid pid\n");
}

void print_settings(void)
{
	printf("timewarn %d\n", timewarn);
	printf("timeout %llu\n", (unsigned long long) timeout);
	printf("noqueue %d\n", noqueue);
	printf("persistent %d\n", persistent);
	printf("ignore_bast %d\n", ignore_bast);
	printf("quiet %d\n", quiet);
	printf("verbose %d\n", verbose);
	printf("maxn %d\n", maxn);
	printf("maxr %d\n", maxr);
	printf("iterations %d\n", iterations);
	printf("minhold %d\n", minhold);
	printf("stress_stop %d\n", stress_stop);
	printf("stress_delay %d\n", stress_delay);
	printf("stress_lock_only %d\n", stress_lock_only);
}

void process_command(int *quit)
{
	char inbuf[132];
	char cmd[32];
	int x = 0, y = 0, z = 0;

	fgets(inbuf, sizeof(inbuf), stdin);

	sscanf(inbuf, "%s %d %d", cmd, &x, &y, &z);

	if (!strncmp(cmd, "EXIT", 4)) {
		*quit = 1;
		unlock_all();
		return;
	}

	if (!strncmp(cmd, "CLOSE", 5)) {
		*quit = 1;
		openclose_ls = 1;
		unlock_all();
		return;
	}

	if (!strncmp(cmd, "kill", 4)) {
		printf("process exiting\n");
		exit(0);
	}

	if (!strncmp(cmd, "lock", 4) && strlen(cmd) == 4) {
		lock(x, y);
		return;
	}

	if (!strncmp(cmd, "unlock", 6) && strlen(cmd) == 6) {
		unlock(x);
		return;
	}

	if (!strncmp(cmd, "unlockf", 7) && strlen(cmd) == 7) {
		unlockf(x);
		return;
	}

	if (!strncmp(cmd, "cancel", 6) && strlen(cmd) == 6) {
		cancel(x);
		return;
	}

	if (!strncmp(cmd, "canceld", 7) && strlen(cmd) == 7) {
		canceld(x, y);
		return;
	}

	if (!strncmp(cmd, "lock_sync", 9) && strlen(cmd) == 9) {
		lock_sync(x, y);
		return;
	}

	if (!strncmp(cmd, "unlock_sync", 11) && strlen(cmd) == 11) {
		unlock_sync(x);
		return;
	}

	if (!strncmp(cmd, "lock-kill", 9) && strlen(cmd) == 9) {
		lock(x, y);
		printf("process exiting\n");
		exit(0);
	}

	if (!strncmp(cmd, "unlock-kill", 11) && strlen(cmd) == 11) {
		unlock(x);
		printf("process exiting\n");
		exit(0);
	}

	if (!strncmp(cmd, "lock-cancel", 11) && strlen(cmd) == 11) {
		lock(x, y);
		/* usleep(1000 * z); */
		cancel(x);
		return;
	}

	if (!strncmp(cmd, "lock-unlockf", 12) && strlen(cmd) == 12) {
		lock(x, y);
		/* usleep(1000 * z); */
		unlockf(x);
		return;
	}

	if (!strncmp(cmd, "ex", 2)) {
		lock(x, LKM_EXMODE);
		return;
	}

	if (!strncmp(cmd, "pr", 2)) {
		lock(x, LKM_PRMODE);
		return;
	}

	if (!strncmp(cmd, "hold", 4) && strlen(cmd) == 4) {
		lock_all(LKM_PRMODE);
		return;
	}

	if (!strncmp(cmd, "hold-kill", 9) && strlen(cmd) == 9) {
		lock_all(LKM_PRMODE);
		exit(0);
	}

	if (!strncmp(cmd, "release", 7) && strlen(cmd) == 7) {
		unlock_all();
		return;
	}

	if (!strncmp(cmd, "release-kill", 12) && strlen(cmd) == 12) {
		unlock_all();
		exit(0);
	}

	if (!strncmp(cmd, "dump", 4) && strlen(cmd) == 4) {
		dump();
		return;
	}

	if (!strncmp(cmd, "stress", 6) && strlen(cmd) == 6) {
		stress(x);
		return;
	}

	if (!strncmp(cmd, "tstress", 7) && strlen(cmd) == 7) {
		tstress(x);
		return;
	}

	if (!strncmp(cmd, "stress_delay", 12) && strlen(cmd) == 12) {
		stress_delay = x;
		return;
	}

	if (!strncmp(cmd, "stress_lock_only", 16) && strlen(cmd) == 16) {
		stress_lock_only = !stress_lock_only;
		printf("stress_lock_only is %s\n", stress_lock_only ? "on" : "off");
		return;
	}

	if (!strncmp(cmd, "ignore_bast", 11) && strlen(cmd) == 11) {
		ignore_bast = !ignore_bast;
		printf("ignore_bast is %s\n", ignore_bast ? "on" : "off");
		return;
	}

	if (!strncmp(cmd, "purge", 5) && strlen(cmd) == 5) {
		purge(x, y);
		return;
	}

	if (!strncmp(cmd, "purgetest", 9) && strlen(cmd) == 9) {
		purgetest(x, y);
		return;
	}

	if (!strncmp(cmd, "noqueue", 7)) {
		noqueue = !noqueue;
		printf("noqueue is %s\n", noqueue ? "on" : "off");
		return;
	}

	if (!strncmp(cmd, "persistent", 10)) {
		persistent = !persistent;
		printf("persistent is %s\n", persistent ? "on" : "off");
		return;
	}

	if (!strncmp(cmd, "minhold", 7)) {
		minhold = x;
		return;
	}

	if (!strncmp(cmd, "timeout", 7)) {
		timeout = (uint64_t) 100 * x; /* dlm takes it in centiseconds */
		printf("timeout is %d\n", x);
		return;
	}

	if (!strncmp(cmd, "quiet", 5)) {
		quiet = !quiet;
		printf("quiet is %d\n", quiet);
		return;
	}

	if (!strncmp(cmd, "verbose", 7)) {
		verbose = !verbose;
		printf("verbose is %d\n", verbose);
		return;
	}

	if (!strncmp(cmd, "help", 4)) {
		print_commands();
		return;
	}

	if (!strncmp(cmd, "settings", 8)) {
		print_settings();
		return;
	}

	printf("unknown command %s\n", cmd);
}

void print_usage(void)
{
	printf("Options:\n");
	printf("\n");
	printf("  -n           The number of locks to work with, default %d\n", MAX_LOCKS);
	printf("  -r           The number of resources to work with, default %d\n", MAX_RESOURCES);
	printf("  -i           Iterations in looping stress test, default 0 is no limit\n");
	printf("  -t           Enable timeout warnings\n");
}

static void decode_arguments(int argc, char **argv)
{
	int cont = 1;
	int optchar;

	while (cont) {
		optchar = getopt(argc, argv, "n:r:i:thVo");

		switch (optchar) {

		case 'n':
			maxn = atoi(optarg);
			break;

		case 'r':
			maxr = atoi(optarg);
			break;

		case 'i':
			iterations = atoi(optarg);
			break;

		case 't':
			timewarn = 1;
			break;

		case 'o':
			openclose_ls = 1;
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case 'V':
			printf("%s (built %s %s)\n", argv[0], __DATE__, __TIME__);
			/* printf("%s\n", REDHAT_COPYRIGHT); */
			exit(EXIT_SUCCESS);
			break;

		case ':':
		case '?':
			fprintf(stderr, "Please use '-h' for usage.\n");
			exit(EXIT_FAILURE);
			break;

		case EOF:
			cont = 0;
			break;

		default:
			fprintf(stderr, "unknown option: %c\n", optchar);
			exit(EXIT_FAILURE);
			break;
		};
	}
}

static void sigterm_handler(int sig)
{
	stress_stop = 1;
}

int main(int argc, char *argv[])
{
	uint32_t major, minor, patch;
	struct lk *lk;
	int i, rv, maxi = 0, quit = 0;

	decode_arguments(argc, argv);

	if (maxn < maxr) {
		printf("number of resources must be >= number of locks\n");
		return -1;
	}
	if (maxn % maxr) {
		printf("number of locks must be multiple of number of resources\n");
		return -1;
	}

	printf("maxn = %d\n", maxn);
	printf("maxr = %d\n", maxr);
	printf("locks per resource = %d\n", maxn / maxr);

	signal(SIGTERM, sigterm_handler);

	client_init();

	locks = malloc(maxn * sizeof(struct lk));
	if (!locks) {
		printf("no mem for %d locks\n", maxn);
		return;
	}
	memset(locks, 0, sizeof(*locks));

	lk = locks;
	for (i = 0; i < maxn; i++) {
		lk->id = i;
		lk->grmode = -1;
		lk->rqmode = -1;
		lk++;
	}

	rv = dlm_kernel_version(&major, &minor, &patch);
	if (rv < 0) {
		printf("can't detect dlm in kernel %d\n", errno);
		return -1;
	}
	printf("dlm kernel version: %u.%u.%u\n", major, minor, patch);
	dlm_library_version(&major, &minor, &patch);
	printf("dlm library version: %u.%u.%u\n", major, minor, patch);

	if (openclose_ls) {
		printf("dlm_open_lockspace...\n");

		dh = dlm_open_lockspace("test");
		if (!dh) {
			printf("dlm_open_lockspace error %lu %d\n",
				(unsigned long)dh, errno);
			return -ENOTCONN;
		}
	} else {
		printf("dlm_new_lockspace...\n");

		dh = dlm_new_lockspace("test", 0600, timewarn ? DLM_LSFL_TIMEWARN : 0);
		if (!dh) {
			printf("dlm_new_lockspace error %lu %d\n",
				(unsigned long)dh, errno);
			return -ENOTCONN;
		}
	}

	rv = dlm_ls_get_fd(dh);
	if (rv < 0) {
		printf("dlm_ls_get_fd error %d %d\n", rv, errno);
		dlm_release_lockspace("test", dh, 1);
		return rv;
	}
	libdlm_fd = rv;

	client_add(libdlm_fd, &maxi);
	client_add(STDIN_FILENO, &maxi);

	if (strstr(argv[0], "dlmstress"))
		stress(iterations);

	printf("Type EXIT to finish, help for usage\n");

	while (1) {
		rv = poll(pollfd, maxi + 1, -1);
		if (rv < 0 && errno == EINTR)
			continue;
		if (rv < 0)
			printf("poll error %d errno %d\n", rv, errno);

		for (i = 0; i <= maxi; i++) {
			if (client[i].fd < 0)
				continue;

			if (pollfd[i].revents & POLLIN) {
				if (pollfd[i].fd == libdlm_fd)
					process_libdlm();
				else if (pollfd[i].fd == STDIN_FILENO)
					process_command(&quit);
			}

			if (pollfd[i].revents & POLLHUP)
				client_dead(i);
		}

		if (quit && all_unlocks_done())
			break;
	}

	if (openclose_ls) {
		printf("dlm_close_lockspace\n");

		rv = dlm_close_lockspace(dh);
		if (rv < 0)
			printf("dlm_close_lockspace error %d %d\n", rv, errno);
	} else {
		printf("dlm_release_lockspace\n");

		rv = dlm_release_lockspace("test", dh, 1);
		if (rv < 0)
			printf("dlm_release_lockspace error %d %d\n", rv, errno);
	}

	return 0;
}

