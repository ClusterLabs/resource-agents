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
#define LOCKS 4

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
		printf("i %d lkid %x grmode %d rqmode %d wait_ast %d\n", i,
			locks[i].lksb.sb_lkid,
			locks[i].grmode,
			locks[i].rqmode,
			locks[i].wait_ast);
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

void process_libdlm(void)
{
	dlm_dispatch(libdlm_fd);
}

void lock(int i, int mode)
{
	char name[DLM_RESNAME_MAXLEN];
	int flags = 0;
	int rv;

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

void unlock(int i)
{
	uint32_t lkid;
	int rv;

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

void loop(int i, int num)
{
	int n;

	for (n = 0; n < num; n++) {
		/*
		lock(i, LKM_PRMODE);
		dlm_dispatch(libdlm_fd);
		unlock(i);
		dlm_dispatch(libdlm_fd);
		*/
		lock_sync(i, LKM_PRMODE);
		unlock_sync(i);
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

	if (!strncmp(cmd, "ex", 2)) {
		lock(x, LKM_EXMODE);
		return;
	}

	if (!strncmp(cmd, "pr", 2)) {
		lock(x, LKM_PRMODE);
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

	if (!strncmp(cmd, "dump", 4)) {
		dump();
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
		printf("EXIT		 - exit program after unlocking any held locks\n");
		printf("kill		 - exit program without unlocking any locks\n");
		printf("lock x mode	 - request/convert lock on resource x\n");
		printf("unlock x 	 - unlock lock on resource x\n");
		printf("lock_sync x mode - synchronous version of lock\n");
		printf("unlock_sync x 	 - synchronous version of unlock\n");
		printf("lock-kill x mode - request/convert lock on resource x, then exit\n");
		printf("unlock-kill x	 - unlock lock on resource x, then exit\n");
		printf("ex x		 - equivalent to: lock x 5\n");
		printf("pr x		 - equivalent to: lock x 3\n");
		printf("hold		 - for x in 0 to MAX, lock x 3\n");
		printf("release		 - for x in 0 to MAX, unlock x\n");
		printf("dump		 - show info for all resources\n");
		printf("loop x n	 - lock_sync x PR / unlock_sync x, n times\n");
		printf("hammer n	 - loop doing random lock/unlock on all locks, n times");
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

	printf("Type EXIT to finish\n");

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

