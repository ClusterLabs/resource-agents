/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2006 Red Hat, Inc.  All rights reserved.
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

static dlm_lshandle_t *dh;
static int libdlm_fd;
static int noqueue = 1;

#define MAX_CLIENTS 4
#define LOCKS 16

struct client {
	int fd;
	char type[32];
};

static int client_size = MAX_CLIENTS;
static struct client client[MAX_CLIENTS];
static struct pollfd pollfd[MAX_CLIENTS];

struct lk {
	int id;
	int rqmode;
	int grmode;
	int wait_ast;
	struct dlm_lksb lksb;
};

struct lk locks[LOCKS];
struct lk *locks_flood;
int locks_flood_n;
int locks_flood_ast_done;

void unlock(int i);

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

void dump(void)
{
	int i;

	for (i = 0; i < LOCKS; i++) {
		printf("x %d lkid %x grmode %d rqmode %d wait_ast %d\n", i,
			locks[i].lksb.sb_lkid,
			locks[i].grmode,
			locks[i].rqmode,
			locks[i].wait_ast);
	}
}

void dump_flood(void)
{
	struct lk *lk;
	int i;

	if (!locks_flood_n) {
		printf("no current locks_flood\n");
		return;
	}

	for (i = 0; i < locks_flood_n; i++) {
		lk = &locks_flood[i];
		printf("x %d lkid %x grmode %d rqmode %d wait_ast %d\n", i,
			lk->lksb.sb_lkid,
			lk->grmode,
			lk->rqmode,
			lk->wait_ast);
	}
}

void bastfn(void *arg)
{
	struct lk *lk = arg;

	printf("bast %d\n", lk->id);

	unlock(lk->id);
}

void astfn(void *arg)
{
	struct lk *lk = arg;
	int i = lk->id;

	printf("ast %d sb_lkid %x sb_status %x\n", i, lk->lksb.sb_lkid,
	       lk->lksb.sb_status);

	if (!lk->wait_ast) {
		printf("not waiting for ast\n");
		exit(-1);
	}

	lk->wait_ast = 0;

	if (lk->lksb.sb_status == EUNLOCK) {
		memset(&locks[i].lksb, 0, sizeof(struct dlm_lksb));
		locks[i].grmode = -1;
	} else if (lk->lksb.sb_status == EAGAIN) {
		if (locks[i].grmode == -1)
			memset(&locks[i].lksb, 0, sizeof(struct dlm_lksb));
	} else {
		if (lk->lksb.sb_status != 0) {
			printf("unknown sb_status\n");
			exit(-1);
		}

		locks[i].grmode = locks[i].rqmode;
	}

	locks[i].rqmode = -1;
}

void bastfn_flood(void *arg)
{
	struct lk *lk = arg;
	printf("bastfn_flood %d\n", lk->id);
}

void astfn_flood(void *arg)
{
	struct lk *lk = arg;
	int i = lk->id, unlock = 0;

	if (!lk->wait_ast) {
		printf("lk %d not waiting for ast\n", lk->id);
		exit(-1);
	}

	lk->wait_ast = 0;

	if (lk->lksb.sb_status == EUNLOCK) {
		memset(&lk->lksb, 0, sizeof(struct dlm_lksb));
		lk->grmode = -1;
		unlock = 1;
	} else if (lk->lksb.sb_status == EAGAIN) {
		if (lk->grmode == -1)
			memset(&lk->lksb, 0, sizeof(struct dlm_lksb));
	} else {
		if (lk->lksb.sb_status != 0) {
			printf("lk %d unknown sb_status %d\n", lk->id,
				lk->lksb.sb_status);
			exit(-1);
		}

		lk->grmode = lk->rqmode;
	}

	lk->rqmode = -1;
	locks_flood_ast_done++;

	if (locks_flood_ast_done == locks_flood_n) {
		printf("astfn_flood all %d done\n", locks_flood_n);
		if (unlock) {
			free(locks_flood);
			locks_flood = NULL;
			locks_flood_n = 0;
		}
	}
}

void astfn_flood_unlock(void *arg)
{
	struct lk *lk = arg;
	int i = lk->id, unlock = 0, rv;

	if (!lk->wait_ast) {
		printf("lk %d not waiting for ast\n", lk->id);
		exit(-1);
	}

	lk->wait_ast = 0;

	if (lk->lksb.sb_status == EUNLOCK) {
		memset(&lk->lksb, 0, sizeof(struct dlm_lksb));
		lk->grmode = -1;
		lk->rqmode = -1;
		unlock = 1;
		locks_flood_ast_done++;
	} else if (lk->lksb.sb_status == EAGAIN) {
		if (lk->grmode == -1)
			memset(&lk->lksb, 0, sizeof(struct dlm_lksb));
		lk->rqmode = -1;
	} else {
		if (lk->lksb.sb_status != 0) {
			printf("lk %d unknown sb_status %d\n", lk->id,
				lk->lksb.sb_status);
			exit(-1);
		}

		/* lock completed, now unlock it immediately */

		lk->grmode = lk->rqmode;
		lk->rqmode = -1;

		rv = dlm_ls_unlock(dh, lk->lksb.sb_lkid, 0, &lk->lksb,
				   (void *)lk);
		if (!rv) {
			lk->wait_ast = 1;
			lk->rqmode = -1;
		} else {
			char input[32];
			printf("astfn_flood_unlock: dlm_ls_unlock: %d rv %d "
			       "errno %d\n", i, rv, errno);
			printf("press X to exit, D to dispatch, "
			       "other to continue\n");
			fgets(input, 32, stdin);
			if (input[0] == 'X')
				exit(-1);
			else if (input[0] == 'D')
				dlm_dispatch(libdlm_fd);
		}
	}

	if (locks_flood_ast_done == locks_flood_n) {
		printf("astfn_flood_unlock all %d unlocked\n", locks_flood_n);
		if (unlock) {
			free(locks_flood);
			locks_flood = NULL;
			locks_flood_n = 0;
		}
	}
}

void process_libdlm(void)
{
	dlm_dispatch(libdlm_fd);
}

void lock(int i, int mode)
{
	char name[DLM_RESNAME_MAXLEN];
	int flags = 0;
	int rv;

	if (i < 0 || i >= LOCKS)
		return;

	if (noqueue)
		flags |= LKF_NOQUEUE;

	if (locks[i].lksb.sb_lkid)
		flags |= LKF_CONVERT;

	memset(name, 0, sizeof(name));
	snprintf(name, sizeof(name), "test%d", i);

	printf("dlm_ls_lock: %d grmode %d rqmode %d flags %x lkid %x %s\n",
	       i, locks[i].grmode, mode, flags, locks[i].lksb.sb_lkid, name);

	rv = dlm_ls_lock(dh, mode, &locks[i].lksb, flags,
			 name, strlen(name), 0, astfn, (void *) &locks[i],
			 bastfn, NULL);
	if (!rv) {
		locks[i].wait_ast = 1;
		locks[i].rqmode = mode;
	}

	printf("dlm_ls_lock: %d rv %d sb_lkid %x sb_status %x\n",
	       i, rv, locks[i].lksb.sb_lkid, locks[i].lksb.sb_status);
}

void lock_sync(int i, int mode)
{
	char name[DLM_RESNAME_MAXLEN];
	int flags = 0;
	int rv;

	if (i < 0 || i >= LOCKS)
		return;

	if (noqueue)
		flags |= LKF_NOQUEUE;

	if (locks[i].lksb.sb_lkid)
		flags |= LKF_CONVERT;

	memset(name, 0, sizeof(name));
	snprintf(name, sizeof(name), "test%d", i);

	printf("dlm_ls_lock_wait: %d rqmode %d flags %x lkid %x %s\n",
	       i, mode, flags, locks[i].lksb.sb_lkid, name);

	rv = dlm_ls_lock_wait(dh, mode, &locks[i].lksb, flags,
			 name, strlen(name), 0, (void *) &locks[i],
			 bastfn, NULL);

	printf("dlm_ls_lock_wait: %d rv %d sb_lkid %x sb_status %x\n",
	       i, rv, locks[i].lksb.sb_lkid, locks[i].lksb.sb_status);

	if (!rv) {
		locks[i].grmode = mode;
		locks[i].rqmode = -1;
	} else if (rv == EAGAIN) {
		if (locks[i].grmode == -1)
			memset(&locks[i].lksb, 0, sizeof(struct dlm_lksb));
	} else {
		printf("unknown rv %d\n", rv);
		exit(-1);
	}
}

void lock_all(int mode)
{
	int i;

	for (i = 0; i < LOCKS; i++)
		lock(i, mode);
}

void lock_flood(int n, int mode)
{
	struct lk *lk;
	char name[DLM_RESNAME_MAXLEN];
	int flags = 0, rv, i;

	if (noqueue)
		flags |= LKF_NOQUEUE;

	if (locks_flood) {
		printf("unlock_flood required before another lock_flood\n");
		return;
	}

	locks_flood = malloc(n * sizeof(struct lk));
	if (!locks_flood) {
		printf("no mem for %d locks\n", n);
		return;
	}
	locks_flood_n = n;
	locks_flood_ast_done = 0;
	memset(locks_flood, 0, sizeof(*locks_flood));

	for (i = 0; i < n; i++) {
		memset(name, 0, sizeof(name));
		snprintf(name, sizeof(name), "testflood%d", i);
		lk = &locks_flood[i];

		rv = dlm_ls_lock(dh, mode, &lk->lksb, flags,
			 name, strlen(name), 0, astfn_flood, (void *) lk,
			 bastfn_flood, NULL);
		if (!rv) {
			lk->wait_ast = 1;
			lk->rqmode = mode;
		}
	}
}

void unlock(int i)
{
	uint32_t lkid;
	int rv;

	if (i < 0 || i >= LOCKS)
		return;

	lkid = locks[i].lksb.sb_lkid;
	if (!lkid) {
		printf("unlock %d skip zero lkid\n", i);
		return;
	}

	printf("dlm_ls_unlock: %d lkid %x\n", i, lkid);

	rv = dlm_ls_unlock(dh, lkid, 0, &locks[i].lksb, &locks[i]);
	if (!rv) {
		locks[i].wait_ast = 1;
		locks[i].rqmode = -1;
		printf("dlm_ls_unlock: %d\n", i);
	} else {
		char input[32];
		printf("dlm_ls_unlock: %d rv %d errno %d\n", i, rv, errno);
		dump();
		printf("press X to exit, D to dispatch, other to continue\n");
		fgets(input, 32, stdin);
		if (input[0] == 'X')
			exit(-1);
		else if (input[0] == 'D')
			dlm_dispatch(libdlm_fd);
	}
}

void unlock_sync(int i)
{
	uint32_t lkid;
	int rv;

	if (i < 0 || i >= LOCKS)
		return;

	lkid = locks[i].lksb.sb_lkid;
	if (!lkid) {
		printf("unlock %d skip zero lkid\n", i);
		return;
	}

	printf("dlm_ls_unlock_wait: %d lkid %x\n", i, lkid);

	rv = dlm_ls_unlock_wait(dh, lkid, 0, &locks[i].lksb);

	printf("dlm_ls_unlock_wait: %d rv %d sb_status %x\n", i, rv,
	       locks[i].lksb.sb_status);

	memset(&locks[i].lksb, 0, sizeof(struct dlm_lksb));
	locks[i].grmode = -1;
	locks[i].rqmode = -1;
}

void unlock_all(void)
{
	int i;

	for (i = 0; i < LOCKS; i++)
		unlock(i);
}

void unlock_flood(void)
{
	struct lk *lk;
	uint32_t lkid;
	int rv, i;

	if (!locks_flood)
		return;

	if (locks_flood_ast_done != locks_flood_n)
		printf("warning: locks_flood_ast_done %d locks_flood_n %d\n",
			locks_flood_ast_done, locks_flood_n);

	locks_flood_ast_done = 0;

	for (i = 0; i < locks_flood_n; i++) {
		lk = &locks_flood[i];
		lkid = lk->lksb.sb_lkid;
		if (!lkid) {
			printf("unlock_flood %d skip zero lkid\n", i);
			continue;
		}

		rv = dlm_ls_unlock(dh, lkid, 0, &lk->lksb, (void *)lk);
		if (!rv) {
			lk->wait_ast = 1;
			lk->rqmode = -1;
		} else {
			char input[32];
			printf("flood: dlm_ls_unlock: %d rv %d errno %d\n",
				i, rv, errno);
			printf("press X to exit, D to dispatch, "
			       "other to continue\n");
			fgets(input, 32, stdin);
			if (input[0] == 'X')
				exit(-1);
			else if (input[0] == 'D')
				dlm_dispatch(libdlm_fd);
		}
	}
}

void flood(int n, int mode)
{
	struct lk *lk;
	char name[DLM_RESNAME_MAXLEN];
	int flags = 0, rv, i;

	if (noqueue)
		flags |= LKF_NOQUEUE;

	if (locks_flood) {
		printf("unlock_flood required first\n");
		return;
	}

	locks_flood = malloc(n * sizeof(struct lk));
	if (!locks_flood) {
		printf("no mem for %d locks\n", n);
		return;
	}
	locks_flood_n = n;
	locks_flood_ast_done = 0;
	memset(locks_flood, 0, sizeof(*locks_flood));

	for (i = 0; i < n; i++) {
		memset(name, 0, sizeof(name));
		snprintf(name, sizeof(name), "testflood%d", i);
		lk = &locks_flood[i];

		rv = dlm_ls_lock(dh, mode, &lk->lksb, flags,
			 name, strlen(name), 0, astfn_flood_unlock, (void *) lk,
			 bastfn_flood, NULL);
		if (!rv) {
			lk->wait_ast = 1;
			lk->rqmode = mode;
		}
	}
}

void loop(int i, int num)
{
	int n;

	for (n = 0; n < num; n++) {
		lock(i, LKM_PRMODE);
		dlm_dispatch(libdlm_fd);
		unlock(i);
		dlm_dispatch(libdlm_fd);
		/*
		lock_sync(i, LKM_PRMODE);
		unlock_sync(i);
		*/
	}
}

int rand_int(int a, int b)
{
	return a + (int) (((float)(b - a + 1)) * random() / (RAND_MAX+1.0)); 
}

void hammer(int num)
{
	int n, i, busy = 0, lock_ops = 0, unlock_ops = 0;

	while (1) {
		dlm_dispatch(libdlm_fd);

		i = rand_int(0, LOCKS-1);

		if (locks[i].wait_ast) {
			busy++;
			continue;
		}

		if (locks[i].grmode == -1) {
			lock(i, rand_int(0, 5));
			lock_ops++;
		} else {
			unlock(i);
			unlock_ops++;
		}

		if (++n == num)
			break;
	}

	printf("hammer: locks %d unlocks %d busy %d\n",
		lock_ops, unlock_ops, busy);

	unlock_all();;
}

int all_unlocks_done(void)
{
	int i;

	for (i = 0; i < LOCKS; i++) {
		if (locks[i].grmode == -1 && !locks[i].wait_ast)
			continue;
		return 0;
	}
	return 1;
}

void process_command(int *quit)
{
	char inbuf[132];
	char cmd[32];
	int x = 0, y = 0;

	fgets(inbuf, sizeof(inbuf), stdin);

	sscanf(inbuf, "%s %d %d", cmd, &x, &y);

	if (!strncmp(cmd, "EXIT", 4)) {
		*quit = 1;
		unlock_all();
		unlock_flood();
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

	if (!strncmp(cmd, "lock_flood", 10) && strlen(cmd) == 10) {
		lock_flood(x, y);
		return;
	}

	if (!strncmp(cmd, "unlock_flood", 12) && strlen(cmd) == 12) {
		unlock_flood();
		return;
	}

	if (!strncmp(cmd, "unlock_flood-kill", 17) && strlen(cmd) == 17) {
		unlock_flood();
		printf("process exiting\n");
		exit(0);
	}

	if (!strncmp(cmd, "ex", 2)) {
		lock(x, LKM_EXMODE);
		return;
	}

	if (!strncmp(cmd, "pr", 2)) {
		lock(x, LKM_PRMODE);
		return;
	}

	if (!strncmp(cmd, "flood", 5)) {
		flood(x, y);
		return;
	}

	if (!strncmp(cmd, "hold", 4)) {
		lock_all(LKM_PRMODE);
		return;
	}

	if (!strncmp(cmd, "release", 6)) {
		unlock_all();
		return;
	}

	if (!strncmp(cmd, "dump", 4) && strlen(cmd) == 4) {
		dump();
		return;
	}

	if (!strncmp(cmd, "dump_flood", 10) && strlen(cmd) == 10) {
		dump_flood();
		return;
	}

	if (!strncmp(cmd, "loop", 4)) {
		loop(x, y);
		return;
	}

	if (!strncmp(cmd, "hammer", 6)) {
		hammer(x);
		return;
	}

	if (!strncmp(cmd, "noqueue", 7)) {
		noqueue = !noqueue;
		printf("noqueue is %s\n", noqueue ? "on" : "off");
		return;
	}

	if (!strncmp(cmd, "help", 4)) {
		printf("Usage:\n");
		printf("MAX locks is %d (x of 0 to %d)\n", LOCKS, LOCKS-1);
		printf("EXIT		 - exit program after unlocking any held locks\n");
		printf("kill		 - exit program without unlocking any locks\n");
		printf("lock x mode	 - request/convert lock on resource x\n");
		printf("unlock x 	 - unlock lock on resource x\n");
		printf("lock_sync x mode - synchronous version of lock\n");
		printf("unlock_sync x 	 - synchronous version of unlock\n");
		printf("lock-kill x mode - request/convert lock on resource x, then exit\n");
		printf("unlock-kill x	 - unlock lock on resource x, then exit\n");
		printf("lock_flood n mode - request n locks (in flood namespace)\n");
		printf("unlock_flood     - unlock all from lock_flood\n");
		printf("unlock_flood-kill - unlock all from lock_flood and exit\n");
		printf("ex x		 - equivalent to: lock x 5\n");
		printf("pr x		 - equivalent to: lock x 3\n");
		printf("flood n mode     - request n locks, unlock each as it completes\n");
		printf("hold		 - for x in 0 to MAX, lock x 3\n");
		printf("release		 - for x in 0 to MAX, unlock x\n");
		printf("dump		 - show info for all resources\n");
		printf("dump_flood	 - show info for all flood resources\n");
		printf("loop x n	 - lock_sync x PR / unlock_sync x, n times\n");
		printf("hammer n	 - loop doing random lock/unlock on all locks, n times\n");
		printf("noqueue		 - toggle NOQUEUE flag for all requests\n"); 
		return;
	}

	printf("unknown command %s\n", cmd);
}

int main(int argc, char *argv[])
{
	int i, rv, maxi = 0, quit = 0;

	client_init();

	memset(&locks, 0, sizeof(locks));
	for (i = 0; i < LOCKS; i++) {
		locks[i].id = i;
		locks[i].grmode = -1;
		locks[i].rqmode = -1;
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

	printf("Type EXIT to finish, help for usage\n");

	while (1) {
		rv = poll(pollfd, maxi + 1, -1);
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

