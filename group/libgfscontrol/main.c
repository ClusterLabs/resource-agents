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

#include "libgfscontrol.h"
#include "gfs_controld.h"

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

static void init_header(struct gfsc_header *h, int cmd, char *name,
			int extra_len)
{
	memset(h, 0, sizeof(struct gfsc_header));

	h->magic = GFSC_MAGIC;
	h->version = GFSC_VERSION;
	h->len = sizeof(struct gfsc_header) + extra_len;
	h->command = cmd;

	if (name)
		strncpy(h->name, name, GFS_MOUNTGROUP_LEN);
}

int do_dump(int cmd, char *name, char *buf)
{
	struct gfsc_header h, *rh;
	char *reply;
	int reply_len;
	int fd, rv;

	init_header(&h, cmd, name, 0);

	reply_len = sizeof(struct gfsc_header) + GFSC_DUMP_SIZE;
	reply = malloc(reply_len);
	if (!reply) {
		rv = -1;
		goto out;
	}
	memset(reply, 0, reply_len);

	fd = do_connect(GFSC_QUERY_SOCK_PATH);
	if (fd < 0) {
		rv = fd;
		goto out;
	}

	rv = do_write(fd, &h, sizeof(h));
	if (rv < 0)
		goto out_close;

	/* won't always get back the full reply_len */
	do_read(fd, reply, reply_len);

	rh = (struct gfsc_header *)reply;
	rv = rh->data;
	if (rv < 0)
		goto out_close;

	memcpy(buf, (char *)reply + sizeof(struct gfsc_header),
	       GFSC_DUMP_SIZE);
 out_close:
	close(fd);
 out:
	return rv;
}

int gfsc_dump_debug(char *buf)
{
	return do_dump(GFSC_CMD_DUMP_DEBUG, NULL, buf);
}

int gfsc_dump_plocks(char *name, char *buf)
{
	return do_dump(GFSC_CMD_DUMP_PLOCKS, name, buf);
}

int gfsc_node_info(char *name, int nodeid, struct gfsc_node *node)
{
	struct gfsc_header h, *rh;
	char reply[sizeof(struct gfsc_header) + sizeof(struct gfsc_node)];
	int fd, rv;

	init_header(&h, GFSC_CMD_NODE_INFO, name, 0);
	h.data = nodeid;

	memset(reply, 0, sizeof(reply));

	fd = do_connect(GFSC_QUERY_SOCK_PATH);
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

	rh = (struct gfsc_header *)reply;
	rv = rh->data;
	if (rv < 0)
		goto out_close;

	memcpy(node, (char *)reply + sizeof(struct gfsc_header),
	       sizeof(struct gfsc_node));
 out_close:
	close(fd);
 out:
	return rv;
}

int gfsc_mountgroup_info(char *name, struct gfsc_mountgroup *mountgroup)
{
	struct gfsc_header h, *rh;
	char reply[sizeof(struct gfsc_header) + sizeof(struct gfsc_mountgroup)];
	int fd, rv;

	init_header(&h, GFSC_CMD_MOUNTGROUP_INFO, name, 0);

	memset(reply, 0, sizeof(reply));

	fd = do_connect(GFSC_QUERY_SOCK_PATH);
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

	rh = (struct gfsc_header *)reply;
	rv = rh->data;
	if (rv < 0)
		goto out_close;

	memcpy(mountgroup, (char *)reply + sizeof(struct gfsc_header),
	       sizeof(struct gfsc_mountgroup));
 out_close:
	close(fd);
 out:
	return rv;
}

int gfsc_mountgroups(int max, int *count, struct gfsc_mountgroup *mgs)
{
	struct gfsc_header h, *rh;
	char *reply;
	int reply_len;
	int fd, rv, result, mg_count;

	init_header(&h, GFSC_CMD_MOUNTGROUPS, NULL, 0);
	h.data = max;

	reply_len = sizeof(struct gfsc_header) +
		    (max * sizeof(struct gfsc_mountgroup));
	reply = malloc(reply_len);
	if (!reply) {
		rv = -1;
		goto out;
	}
	memset(reply, 0, reply_len);

	fd = do_connect(GFSC_QUERY_SOCK_PATH);
	if (fd < 0) {
		rv = fd;
		goto out;
	}

	rv = do_write(fd, &h, sizeof(h));
	if (rv < 0)
		goto out_close;

	/* won't usually get back the full reply_len */
	do_read(fd, reply, reply_len);

	rh = (struct gfsc_header *)reply;
	result = rh->data;
	if (result < 0 && result != -E2BIG) {
		rv = result;
		goto out_close;
	}

	if (result == -E2BIG) {
		*count = -E2BIG;
		mg_count = max;
	} else {
		*count = result;
		mg_count = result;
	}
	rv = 0;

	memcpy(mgs, (char *)reply + sizeof(struct gfsc_header),
	       mg_count * sizeof(struct gfsc_mountgroup));
 out_close:
	close(fd);
 out:
	return rv;
}

int gfsc_mountgroup_nodes(char *name, int type, int max, int *count,
			 struct gfsc_node *nodes)
{
	struct gfsc_header h, *rh;
	char *reply;
	int reply_len;
	int fd, rv, result, node_count;

	init_header(&h, GFSC_CMD_MOUNTGROUP_NODES, name, 0);
	h.option = type;
	h.data = max;

	reply_len = sizeof(struct gfsc_header) +
		    (max * sizeof(struct gfsc_node));
	reply = malloc(reply_len);
	if (!reply) {
		rv = -1;
		goto out;
	}
	memset(reply, 0, reply_len);

	fd = do_connect(GFSC_QUERY_SOCK_PATH);
	if (fd < 0) {
		rv = fd;
		goto out;
	}

	rv = do_write(fd, &h, sizeof(h));
	if (rv < 0)
		goto out_close;

	/* won't usually get back the full reply_len */
	do_read(fd, reply, reply_len);

	rh = (struct gfsc_header *)reply;
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

	memcpy(nodes, (char *)reply + sizeof(struct gfsc_header),
	       node_count * sizeof(struct gfsc_node));
 out_close:
	close(fd);
 out:
	return rv;
}

int gfsc_fs_connect(void)
{
	return do_connect(GFSC_SOCK_PATH);
}

void gfsc_fs_disconnect(int fd)
{
	close(fd);
}

int gfsc_fs_join(int fd, struct gfsc_mount_args *ma)
{
	char msg[sizeof(struct gfsc_header) + sizeof(struct gfsc_mount_args)];
	struct gfsc_header *h = (struct gfsc_header *)msg;
	char *name = strstr(ma->table, ":") + 1;

	init_header(h, GFSC_CMD_FS_JOIN, name, sizeof(struct gfsc_mount_args));

	memcpy(msg + sizeof(struct gfsc_header), ma,
	       sizeof(struct gfsc_mount_args));

	return do_write(fd, msg, sizeof(msg));
}

int gfsc_fs_remount(int fd, struct gfsc_mount_args *ma)
{
	char msg[sizeof(struct gfsc_header) + sizeof(struct gfsc_mount_args)];
	struct gfsc_header *h = (struct gfsc_header *)msg;
	char *name = strstr(ma->table, ":") + 1;

	init_header(h, GFSC_CMD_FS_REMOUNT, name,
		    sizeof(struct gfsc_mount_args));

	memcpy(msg + sizeof(struct gfsc_header), ma,
	       sizeof(struct gfsc_mount_args));

	return do_write(fd, msg, sizeof(msg));
}

int gfsc_fs_result(int fd, int *result, struct gfsc_mount_args *ma)
{
	char reply[sizeof(struct gfsc_header) + sizeof(struct gfsc_mount_args)];
	struct gfsc_header *h = (struct gfsc_header *)reply;
	int rv;

	rv = do_read(fd, reply, sizeof(reply));
	if (rv < 0)
		goto out;

	*result = h->data;

	memcpy(ma, reply + sizeof(struct gfsc_header),
	       sizeof(struct gfsc_mount_args));
 out:
	return rv;
}

int gfsc_fs_mount_done(int fd, struct gfsc_mount_args *ma, int result)
{
	char msg[sizeof(struct gfsc_header) + sizeof(struct gfsc_mount_args)];
	struct gfsc_header *h = (struct gfsc_header *)msg;
	char *name = strstr(ma->table, ":") + 1;

	init_header(h, GFSC_CMD_FS_MOUNT_DONE, name,
		    sizeof(struct gfsc_mount_args));

	h->data = result;

	memcpy(msg + sizeof(struct gfsc_header), ma,
	       sizeof(struct gfsc_mount_args));

	return do_write(fd, msg, sizeof(msg));
}

int gfsc_fs_leave(struct gfsc_mount_args *ma, int reason)
{
	char msg[sizeof(struct gfsc_header) + sizeof(struct gfsc_mount_args)];
	struct gfsc_header *h = (struct gfsc_header *)msg;
	char *name = strstr(ma->table, ":") + 1;
	int fd;

	init_header(h, GFSC_CMD_FS_LEAVE, name,
		    sizeof(struct gfsc_mount_args));

	h->data = reason;

	memcpy(msg + sizeof(struct gfsc_header), ma,
	       sizeof(struct gfsc_mount_args));

	fd = do_connect(GFSC_SOCK_PATH);
	if (fd < 0)
		return fd;

	return do_write(fd, msg, sizeof(msg));
}

