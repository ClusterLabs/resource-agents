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
static int noqueue = 1;
static int persistent = 0;
static int quiet = 1;
static int verbose = 0;
static int bast_cb;
static int maxn = MAX_LOCKS;
static int maxr = MAX_RESOURCES;
static int iterations;
static int stress_stop = 0;

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
	struct dlm_lksb lksb;
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
	struct lk *lk;
	int i;

	for (i = 0; i < maxn; i++) {
		lk = get_lock(i);
		printf("x %2d lkid %08x gr %2d rq %2d wait_ast %d last op %s  \t%s\n",
			i,
			lk->lksb.sb_lkid,
			lk->grmode,
			lk->rqmode,
			lk->wait_ast,
			op_str(lk->lastop),
			status_str(lk->last_status));
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

	if (skip)
		printf("    bast: skip    %3d\t%x\n", lk->id, lk->lksb.sb_lkid);
	else {
		printf("    bast: unlockf %3d\t%x\n", lk->id, lk->lksb.sb_lkid);
		unlockf(lk->id);
		lk->lastop = Op_unlockf;
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
	if (bast_cb)
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
		memset(&lk->lksb, 0, sizeof(struct dlm_lksb));
		lk->grmode = -1;
		lk->wait_ast = 0;

	} else if (lk->lksb.sb_status == ECANCEL) {
		if (lk->grmode == -1) {
			memset(&lk->lksb, 0, sizeof(struct dlm_lksb));
			lk->wait_ast = 0;
		} else {
			if (lk->lastop != Op_unlock && lk->lastop != Op_unlockf)
				lk->wait_ast = 0;
		}

	} else if (lk->lksb.sb_status == EAGAIN) {
		if (lk->grmode == -1) {
			memset(&lk->lksb, 0, sizeof(struct dlm_lksb));
			lk->wait_ast = 0;
		} else {
			if (lk->lastop != Op_unlockf)
				lk->wait_ast = 0;
		}

	} else {
		if (lk->lksb.sb_status != 0) {
			printf("unknown sb_status %x\n", lk->lksb.sb_status);
			exit(-1);
		}

		if (lk->lastop != Op_unlockf)
			lk->wait_ast = 0;

		lk->grmode = lk->rqmode;
	}

	lk->rqmode = -1;
}

/* EBUSY from dlm_ls_lock() is expected sometimes, e.g. lock, cancel, lock;
   the first lock is successful and the app gets the status back,
   and issues the second lock before the reply for the overlapping
   cancel (which did nothing) has been received in the dlm. */

void lock(int i, int mode)
{
	char name[DLM_RESNAME_MAXLEN];
	struct lk *lk;
	int flags = 0;
	int rv;

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

	log_verbose("lock: %d grmode %d rqmode %d flags %x lkid %x %s\n",
	            i, lk->grmode, mode, flags, lk->lksb.sb_lkid, name);

	rv = dlm_ls_lock(dh, mode, &lk->lksb, flags, name, strlen(name), 0,
			 astfn, (void *) lk, bastfn, NULL);
	if (!rv) {
		lk->wait_ast = 1;
		lk->rqmode = mode;
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
	_unlock(i, 0);
	lk->rqmode = -1;
	lk->lastop = Op_unlock;
}

void unlockf(int i)
{
	struct lk *lk = get_lock(i);
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
		lk->lastop = Op_unlock;
	}
}

void stress(int num)
{
	int i, o, op, skip;
	unsigned int n, skips, lock_ops, unlock_ops, unlockf_ops, cancel_ops;
	struct lk *lk;

	n = skips = lock_ops = unlock_ops = unlockf_ops = cancel_ops = 0;

	while (!stress_stop) {
		process_libdlm();

		if (++n == num)
			break;

		i = rand_int(0, maxn-1);
		lk = get_lock(i);
		if (!lk)
			continue;

		o = rand_int(0, 5);
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

	printf("skip %d lock %d unlock %d unlockf %d cancel %d\n",
		skips, lock_ops, unlock_ops, unlockf_ops, cancel_ops);
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
	printf("lock_sync x mode - synchronous version of lock\n");
	printf("unlock_sync x 	 - synchronous version of unlock\n");
	printf("ex x		 - equivalent to: lock x 5\n");
	printf("pr x		 - equivalent to: lock x 3\n");
	printf("hold		 - for x in 0 to max, lock x 3\n");
	printf("release		 - for x in 0 to max, unlock x\n");
	printf("stress n	 - loop doing random lock/unlock/unlockf/cancel on all locks, n times\n");
	printf("dump		 - show info for all locks\n");
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

	if (!strncmp(cmd, "stress", 6)) {
		stress(x);
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

	printf("unknown command %s\n", cmd);
}

void print_usage(void)
{
	printf("Options:\n");
	printf("\n");
	printf("  -n           The number of resources to work with, default %d\n", MAX_LOCKS);
	printf("  -i           Iterations in looping stress test, default 0 is no limit\n");
}

static void decode_arguments(int argc, char **argv)
{
	int cont = 1;
	int optchar;

	while (cont) {
		optchar = getopt(argc, argv, "n:r:i:hV");

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

int main(int argc, char *argv[])
{
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

	printf("Joining test lockspace...\n");

	dh = dlm_create_lockspace("test", 0600);
	if (!dh) {
		printf("dlm_create_lockspace error %d %d\n", (int) dh, errno);
		return -ENOTCONN;
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

	printf("dlm_release_lockspace\n");

	rv = dlm_release_lockspace("test", dh, 1);
	if (rv < 0)
		printf("dlm_release_lockspace error %d %d\n", rv, errno);

	return 0;
}

