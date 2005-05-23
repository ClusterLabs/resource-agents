/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This library is free software; you can redistribute it and/or
**  modify it under the terms of the GNU Lesser General Public
**  License as published by the Free Software Foundation; either
**  version 2 of the License, or (at your option) any later version.
**
**  This library is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
**  Lesser General Public License for more details.
**
**  You should have received a copy of the GNU Lesser General Public
**  License along with this library; if not, write to the Free Software
**  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
**
*******************************************************************************
******************************************************************************/

#include <sys/types.h>
#include <sys/un.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>
#include <errno.h>

#include "groupd.h"
#include "libgroup.h"

#define LIBGROUP_MAGIC	0x67727570
#define MAXARGS		100  /* FIXME */
#define MAXLINE		256

#define VALIDATE_HANDLE(h) \
do { \
	if (!(h) || (h)->magic != LIBGROUP_MAGIC) { \
		errno = EINVAL; \
		return -1; \
	} \
} while (0)


static void make_args(char *buf, int *argc, char **argv, char sep)
{
	char *p = buf;
	int i;

	argv[0] = p;

	for (i = 1; i < MAXARGS; i++) {
		p = strchr(buf, sep);
		if (!p)
			break;
		*p = '\0';
		argv[i] = p + 1;
		buf = p + 1;
	}
	*argc = i;
}

struct group_handle
{
	int magic;
	int fd;
	int level;
	void *private;
	group_callbacks_t cbs;
	char prog_name[32];
};

static int _joinleave(group_handle_t handle, char *name, char *info, char *cmd)
{
	char buf[MAXLINE];
	char ibuf[GROUP_INFO_LEN];
	int rv;
	struct group_handle *h = (struct group_handle *) handle;
	VALIDATE_HANDLE(h);

	memset(buf, 0, sizeof(buf));
	memset(ibuf, 0, sizeof(ibuf));

	if (info) {
		snprintf(ibuf, GROUP_INFO_LEN, "%s", info);
		ibuf[GROUP_INFO_LEN-1] = '\0';
		snprintf(buf, sizeof(buf), "%s %s %s", cmd, name, ibuf);
	} else
		snprintf(buf, sizeof(buf), "%s %s", cmd, name);

	rv = write(h->fd, buf, strlen(buf));
	if (rv != strlen(buf))
		return -1;
	return 0;
}

int group_join(group_handle_t handle, char *name, char *info)
{
	return _joinleave(handle, name, info, "join");
}

int group_leave(group_handle_t handle, char *name, char *info)
{
	return _joinleave(handle, name, info, "leave");
}

int group_done(group_handle_t handle, char *name, int event_nr)
{
	char buf[MAXLINE];
	int rv;
	struct group_handle *h = (struct group_handle *) handle;
	VALIDATE_HANDLE(h);

	memset(buf, 0, sizeof(buf));
	snprintf(buf, sizeof(buf), "done %s %d", name, event_nr);

	rv = write(h->fd, buf, strlen(buf));
	if (rv != strlen(buf))
		return -1;
	return 0;
}

static int connect_groupd(void)
{
	struct sockaddr_un sun;
	socklen_t addrlen;
	int rv, fd;

	fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		goto out;

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strcpy(&sun.sun_path[1], GROUPD_SOCK_PATH);
	addrlen = sizeof(sa_family_t) + strlen(sun.sun_path+1) + 1;

	rv = connect(fd, (struct sockaddr *) &sun, addrlen);
	if (rv < 0) {
		close(fd);
		fd = rv;
	}
 out:
	return fd;
}

group_handle_t group_init(void *private, char *prog_name, int level,
			  group_callbacks_t *cbs)
{
	struct group_handle *h;
	char buf[MAXLINE];
	int rv, saved_errno;

	h = malloc(sizeof(struct group_handle));
	if (!h)
		return NULL;

	h->magic = LIBGROUP_MAGIC;
	h->private = private;
	h->cbs = *cbs;
	h->level = level;
	strncpy(h->prog_name, prog_name, 32);

	h->fd = connect_groupd();
	if (h->fd < 0)
		goto fail;

	memset(buf, 0, sizeof(buf));
	snprintf(buf, sizeof(buf), "setup %s %d", prog_name, level);

	rv = write(h->fd, &buf, strlen(buf));
	if (rv < 0)
		goto fail;

	return (group_handle_t) h;

 fail:
	saved_errno = errno;
	close(h->fd);
	free(h);
	h = NULL;
	errno = saved_errno;
	return NULL;
}

int group_exit(group_handle_t handle)
{
	struct group_handle *h = (struct group_handle *) handle;
	VALIDATE_HANDLE(h);
	h->magic = 0;
	close(h->fd);
	free(h);
	return 0;
}

int group_get_fd(group_handle_t handle)
{
	struct group_handle *h = (struct group_handle *) handle;
	VALIDATE_HANDLE(h);
	return h->fd;
}

int group_dispatch(group_handle_t handle)
{
	char buf[MAXLINE], *argv[MAXARGS], *act;
	int argc, rv, i, count, *nodeids;
	struct group_handle *h = (struct group_handle *) handle;
	VALIDATE_HANDLE(h);

	memset(buf, 0, sizeof(buf));

	rv = read(h->fd, &buf, sizeof(buf));

	make_args(buf, &argc, argv, ' ');
	act = argv[0];

	if (!strcmp(act, "stop")) {
		h->cbs.stop(h, h->private, argv[1]);

	} else if (!strcmp(act, "start")) {
		count = argc - 4;
		nodeids = malloc(count * sizeof(int));
		for (i = 4; i < argc; i++)
			nodeids[i-4] = atoi(argv[i]);
		h->cbs.start(h, h->private, argv[1], atoi(argv[2]),
			     atoi(argv[3]), count, nodeids);
		free(nodeids);

	} else if (!strcmp(act, "finish")) {
		h->cbs.finish(h, h->private, argv[1], atoi(argv[2]));

	} else if (!strcmp(act, "terminate")) {
		h->cbs.terminate(h, h->private, argv[1]);

	} else if (!strcmp(act, "set_id")) {
		h->cbs.set_id(h, h->private, argv[1], atoi(argv[2]));
	}

	return 0;
}

int group_get_groups(int max, int *count, group_data_t *data)
{
	char buf[MAXLINE];
	int fd, rv, len;

	fd = connect_groupd();
	if (fd < 0)
		return fd;

	memset(buf, 0, sizeof(buf));
	snprintf(buf, sizeof(buf), "get_groups %d", max);

	rv = write(fd, &buf, strlen(buf));
	if (rv < 0)
		goto out;

	len = max * sizeof(group_data_t);

	rv = read(fd, data, len);
	if (rv > 0) {
		*count = rv / sizeof(group_data_t);
		rv = 0;
	}
 out:
	close(fd);
	return rv;
}

int group_get_group(int level, char *name, group_data_t *data)
{
	char buf[MAXLINE];
	int fd, rv, len;

	fd = connect_groupd();
	if (fd < 0)
		return fd;

	memset(buf, 0, sizeof(buf));
	snprintf(buf, sizeof(buf), "get_group %d %s", level, name);

	rv = write(fd, &buf, strlen(buf));
	if (rv < 0)
		goto out;

	rv = read(fd, data, sizeof(group_data_t));
	rv = 0;
 out:
	close(fd);
	return rv;
}

static int _info(int level, char *name, int nodeid, char *info, char *cmd)
{
	char buf[MAXLINE];
	int fd, rv, len;

	fd = connect_groupd();
	if (fd < 0)
		return fd;

	memset(buf, 0, sizeof(buf));
	snprintf(buf, sizeof(buf), "%s %d %s %d", cmd, level, name, nodeid);

	rv = write(fd, buf, strlen(buf));
	if (rv != strlen(buf)) {
		rv = -1;
		goto out;
	}

	memset(buf, 0, sizeof(buf));

	rv = read(fd, buf, sizeof(buf));
	if (rv != GROUP_INFO_LEN) {
		rv = -1;
		goto out;
	}

	rv = 0;
	memcpy(info, buf, GROUP_INFO_LEN);
 out:
	close(fd);
	return rv;
}

int group_join_info(int level, char *name, int nodeid, char *info)
{
	return _info(level, name, nodeid, info, "join_info");
}

int group_leave_info(int level, char *name, int nodeid, char *info)
{
	return _info(level, name, nodeid, info, "leave_info");
}

