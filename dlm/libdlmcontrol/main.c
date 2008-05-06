/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2008 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <linux/dlmconstants.h>
#include "dlm_controld.h"
#include "libdlmcontrol.h"

static int do_read(int fd, void *buf, size_t count)
{
	int rv, off = 0;

	while (off < count) {
		rv = read(fd, buf + off, count - off);
		if (rv == 0)
			return -1;
		if (rv == -1 && errno == EINTR)
			continue;
		if (rv == -1)
			return -1;
		off += rv;
	}
	return 0;
}

static int do_write(int fd, void *buf, size_t count)
{
	int rv, off = 0;

 retry:
	rv = write(fd, buf + off, count);
	if (rv == -1 && errno == EINTR)
		goto retry;
	if (rv < 0) {
		return rv;
	}

	if (rv != count) {
		count -= rv;
		off += rv;
		goto retry;
	}
	return 0;
}

static int do_connect(char *sock_path)
{
	struct sockaddr_un sun;
	socklen_t addrlen;
	int rv, fd;

	fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		goto out;

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strcpy(&sun.sun_path[1], sock_path);
	addrlen = sizeof(sa_family_t) + strlen(sun.sun_path+1) + 1;

	rv = connect(fd, (struct sockaddr *) &sun, addrlen);
	if (rv < 0) {
		close(fd);
		fd = rv;
	}
 out:
	return fd;
}

static void init_header(struct dlmc_header *h, int cmd, char *name,
			int extra_len)
{
	memset(h, 0, sizeof(struct dlmc_header));

	h->magic = DLMC_MAGIC;
	h->version = DLMC_VERSION;
	h->len = sizeof(struct dlmc_header) + extra_len;
	h->command = cmd;

	if (name)
		strncpy(h->name, name, DLM_LOCKSPACE_LEN);
}

int do_dump(int cmd, char *name, char *buf)
{
	struct dlmc_header h, *rh;
	char *reply;
	int reply_len;
	int fd, rv;

	init_header(&h, cmd, name, 0);

	reply_len = sizeof(struct dlmc_header) + DLMC_DUMP_SIZE;
	reply = malloc(reply_len);
	if (!reply) {
		rv = -1;
		goto out;
	}
	memset(reply, 0, reply_len);

	fd = do_connect(DLMC_QUERY_SOCK_PATH);
	if (fd < 0) {
		rv = fd;
		goto out;
	}

	rv = do_write(fd, &h, sizeof(h));
	if (rv < 0)
		goto out_close;

	/* won't always get back the full reply_len */
	do_read(fd, reply, reply_len);

	rh = (struct dlmc_header *)reply;
	rv = rh->data;
	if (rv < 0)
		goto out_close;

	memcpy(buf, (char *)reply + sizeof(struct dlmc_header),
	       DLMC_DUMP_SIZE);
 out_close:
	close(fd);
 out:
	return rv;
}

int dlmc_dump_debug(char *buf)
{
	return do_dump(DLMC_CMD_DUMP_DEBUG, NULL, buf);
}

int dlmc_dump_plocks(char *name, char *buf)
{
	return do_dump(DLMC_CMD_DUMP_PLOCKS, name, buf);
}

int dlmc_node_info(char *name, int nodeid, struct dlmc_node *node)
{
	struct dlmc_header h, *rh;
	char reply[sizeof(struct dlmc_header) + sizeof(struct dlmc_node)];
	int fd, rv;

	init_header(&h, DLMC_CMD_NODE_INFO, name, 0);
	h.data = nodeid;

	memset(reply, 0, sizeof(reply));

	fd = do_connect(DLMC_QUERY_SOCK_PATH);
	if (fd < 0) {
		rv = fd;
		goto out;
	}

	rv = do_write(fd, &h, sizeof(h));
	if (rv < 0)
		goto out_close;

	rv = do_read(fd, reply, sizeof(reply));
	if (rv < 0)
		goto out_close;

	rh = (struct dlmc_header *)reply;
	rv = rh->data;
	if (rv < 0)
		goto out_close;

	memcpy(node, (char *)reply + sizeof(struct dlmc_header),
	       sizeof(struct dlmc_node));
 out_close:
	close(fd);
 out:
	return rv;
}

int dlmc_lockspace_info(char *name, struct dlmc_lockspace *lockspace)
{
	struct dlmc_header h, *rh;
	char reply[sizeof(struct dlmc_header) + sizeof(struct dlmc_lockspace)];
	int fd, rv;

	init_header(&h, DLMC_CMD_LOCKSPACE_INFO, name, 0);

	memset(reply, 0, sizeof(reply));

	fd = do_connect(DLMC_QUERY_SOCK_PATH);
	if (fd < 0) {
		rv = fd;
		goto out;
	}

	rv = do_write(fd, &h, sizeof(h));
	if (rv < 0)
		goto out_close;

	rv = do_read(fd, reply, sizeof(reply));
	if (rv < 0)
		goto out_close;

	rh = (struct dlmc_header *)reply;
	rv = rh->data;
	if (rv < 0)
		goto out_close;

	memcpy(lockspace, (char *)reply + sizeof(struct dlmc_header),
	       sizeof(struct dlmc_lockspace));
 out_close:
	close(fd);
 out:
	return rv;
}

int dlmc_lockspaces(int max, int *count, struct dlmc_lockspace *lss)
{
	struct dlmc_header h, *rh;
	char *reply;
	int reply_len;
	int fd, rv, result, ls_count;

	init_header(&h, DLMC_CMD_LOCKSPACES, NULL, 0);
	h.data = max;

	reply_len = sizeof(struct dlmc_header) +
		    (max * sizeof(struct dlmc_lockspace));
	reply = malloc(reply_len);
	if (!reply) {
		rv = -1;
		goto out;
	}
	memset(reply, 0, reply_len);

	fd = do_connect(DLMC_QUERY_SOCK_PATH);
	if (fd < 0) {
		rv = fd;
		goto out;
	}

	rv = do_write(fd, &h, sizeof(h));
	if (rv < 0)
		goto out_close;

	/* won't usually get back the full reply_len */
	do_read(fd, reply, reply_len);

	rh = (struct dlmc_header *)reply;
	result = rh->data;
	if (result < 0 && result != -E2BIG) {
		rv = result;
		goto out_close;
	}

	if (result == -E2BIG) {
		*count = -E2BIG;
		ls_count = max;
	} else {
		*count = result;
		ls_count = result;
	}
	rv = 0;

	memcpy(lss, (char *)reply + sizeof(struct dlmc_header),
	       ls_count * sizeof(struct dlmc_lockspace));
 out_close:
	close(fd);
 out:
	return rv;
}

int dlmc_lockspace_nodes(char *name, int type, int max, int *count,
			 struct dlmc_node *nodes)
{
	struct dlmc_header h, *rh;
	char *reply;
	int reply_len;
	int fd, rv, result, node_count;

	init_header(&h, DLMC_CMD_LOCKSPACE_NODES, name, 0);
	h.option = type;
	h.data = max;

	reply_len = sizeof(struct dlmc_header) +
		    (max * sizeof(struct dlmc_node));
	reply = malloc(reply_len);
	if (!reply) {
		rv = -1;
		goto out;
	}
	memset(reply, 0, reply_len);

	fd = do_connect(DLMC_QUERY_SOCK_PATH);
	if (fd < 0) {
		rv = fd;
		goto out;
	}

	rv = do_write(fd, &h, sizeof(h));
	if (rv < 0)
		goto out_close;

	/* won't usually get back the full reply_len */
	do_read(fd, reply, reply_len);

	rh = (struct dlmc_header *)reply;
	result = rh->data;
	if (result < 0 && result != -E2BIG) {
		rv = result;
		goto out_close;
	}

	if (result == -E2BIG) {
		*count = -E2BIG;
		node_count = max;
	} else {
		*count = result;
		node_count = result;
	}
	rv = 0;

	memcpy(nodes, (char *)reply + sizeof(struct dlmc_header),
	       node_count * sizeof(struct dlmc_node));
 out_close:
	close(fd);
 out:
	return rv;
}

int dlmc_fs_connect(void)
{
	return do_connect(DLMC_SOCK_PATH);
}

void dlmc_fs_disconnect(int fd)
{
	close(fd);
}

int dlmc_fs_register(int fd, char *name)
{
	struct dlmc_header h;

	init_header(&h, DLMC_CMD_FS_REGISTER, name, 0);

	return do_write(fd, &h, sizeof(h));
}

int dlmc_fs_unregister(int fd, char *name)
{
	struct dlmc_header h;

	init_header(&h, DLMC_CMD_FS_UNREGISTER, name, 0);

	return do_write(fd, &h, sizeof(h));
}

int dlmc_fs_notified(int fd, char *name, int nodeid)
{
	struct dlmc_header h;

	init_header(&h, DLMC_CMD_FS_NOTIFIED, name, 0);
	h.data = nodeid;

	return do_write(fd, &h, sizeof(h));
}

int dlmc_fs_result(int fd, char *name, int *type, int *nodeid, int *result)
{
	struct dlmc_header h;
	int rv;

	rv = do_read(fd, &h, sizeof(h));
	if (rv < 0)
		goto out;

	strncpy(name, h.name, DLM_LOCKSPACE_LEN);
	*type = h.command;
	*nodeid = h.option;
	*result = h.data;
 out:
	return rv;
}

int dlmc_deadlock_check(char *name)
{
	struct dlmc_header h;
	int fd, rv;

	init_header(&h, DLMC_CMD_DEADLOCK_CHECK, name, 0);

	fd = do_connect(DLMC_SOCK_PATH);
	if (fd < 0) {
		rv = fd;
		goto out;
	}

	rv = do_write(fd, &h, sizeof(h));
	close(fd);
 out:
	return rv;
}

