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
#include <dirent.h>

#include "dlm_node.h"

#include "dlm_daemon.h"

static int dir_members[MAX_GROUP_MEMBERS];
static int dir_members_count;

#define DLM_SYSFS_DIR "/sys/kernel/dlm"
#define LS_DIR "/config/dlm/cluster/spaces"


/*
 * ioctl interface only used for setting up addr/nodeid info
 * with set_local and set_node
 */

int do_command(int op, struct dlm_node_ioctl *ni);

static void init_ni(struct dlm_node_ioctl *ni)
{
	memset(ni, 0, sizeof(struct dlm_node_ioctl));

	ni->version[0] = DLM_NODE_VERSION_MAJOR;
	ni->version[1] = DLM_NODE_VERSION_MINOR;
	ni->version[2] = DLM_NODE_VERSION_PATCH;
}

static void set_ipaddr(struct dlm_node_ioctl *ni, char *ip)
{
	struct sockaddr_in sin;
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	inet_pton(AF_INET, ip, &sin.sin_addr);
	memcpy(ni->addr, &sin, sizeof(sin));
}

int set_node(int argc, char **argv)
{
	struct dlm_node_ioctl ni;

	if (argc < 2 || argc > 3)
		return -EINVAL;

	init_ni(&ni);
	ni.nodeid = atoi(argv[0]);
	set_ipaddr(&ni, argv[1]);
	if (argc > 2)
		ni.weight = atoi(argv[2]);
	else
		ni.weight = 1;
	return do_command(DLM_SET_NODE, &ni);
}

int set_local(int argc, char **argv)
{
	struct dlm_node_ioctl ni;

	if (argc < 2 || argc > 3)
		return -EINVAL;

	init_ni(&ni);
	ni.nodeid = atoi(argv[0]);
	set_ipaddr(&ni, argv[1]);
	if (argc > 2)
		ni.weight = atoi(argv[2]);
	return do_command(DLM_SET_LOCAL, &ni);
}

static int do_sysfs(char *name, char *file, char *val)
{
	char fname[512];
	int rv, fd, len;

	sprintf(fname, "%s/%s/%s", DLM_SYSFS_DIR, name, file);

	fd = open(fname, O_WRONLY);
	if (fd < 0) {
		printf("open \"%s\" error %d %d\n", fname, fd, errno);
		return -1;
	}

	printf("write \"%s\" to \"%s\"\n", val, fname);

	len = strlen(val) + 1;
	rv = write(fd, val, len);
	if (rv != len) {
		printf("write %d error %d %d\n", len, rv, errno);
		rv = -1;
	} else
		rv = 0;

	close(fd);
	return rv;
}

int set_control(int argc, char **argv)
{
	return do_sysfs(argv[0], "control", argv[1]);
}

int set_event_done(int argc, char **argv)
{
	return do_sysfs(argv[0], "event_done", argv[1]);
}

static int update_dir_members(char *name)
{
	char path[PATH_MAX];
	DIR *d;
	struct dirent *de;
	int i = 0;

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX, "%s/%s/nodes", LS_DIR, name);

	d = opendir(path);
	if (!d) {
		log_error("%s: opendir failed: %d", path, errno);
		return -1;
	}

	memset(dir_members, 0, sizeof(dir_members));
	dir_members_count = 0;

	/* FIXME: we should probably read the nodeid in each dir instead */

	while ((de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;
		dir_members[i++] = atoi(de->d_name);
		log_error("dir_member %d", dir_members[i-1]);
	}

	dir_members_count = i;
	return 0;
}

static int id_exists(int id, int count, int *array)
{
	int i;
	for (i = 0; i < count; i++) {
		if (array[i] == id)
			return 1;
	}
	return 0;
}

static int create_path(char *path)
{
	mode_t old_umask;
	int rv;

	old_umask = umask(0022);
	rv = mkdir(path, 0777);
	umask(old_umask);

	if (rv < 0) {
		log_error("%s: mkdir failed: %d", path, errno);
		if (errno == EEXIST)
			rv = 0;
	}
	return rv;
}

int set_members(char *name, int new_count, int *new_members)
{
	char path[PATH_MAX];
	char buf[32];
	int i, fd, rv, id, old_count, *old_members;

	rv = update_dir_members(name);
	if (rv)
		return rv;

	old_members = dir_members;
	old_count = dir_members_count;

	for (i = 0; i < old_count; i++) {
		id = old_members[i];
		if (id_exists(id, new_count, new_members))
			continue;

		memset(path, 0, PATH_MAX);
		snprintf(path, PATH_MAX, "%s/%s/nodes/%d", LS_DIR, name, id);

		log_debug("set_members unlink \"%s\"", path);

		rv = unlink(path);
		if (rv) {
			log_error("%s: unlink failed: %d", path, errno);
			return -1;
		}
	}

	for (i = 0; i < new_count; i++) {
		id = new_members[i];
		if (id_exists(id, old_count, old_members))
			continue;

		memset(path, 0, PATH_MAX);
		snprintf(path, PATH_MAX, "%s/%s/nodes/%d", LS_DIR, name, id);

		log_debug("set_members mkdir \"%s\"", path);

		rv = create_path(path);
		if (rv)
			return -1;

		memset(path, 0, PATH_MAX);
		snprintf(path, PATH_MAX, "%s/%s/nodes/%d/nodeid", LS_DIR, name,
			 id);

		fd = open(path, O_WRONLY);
		if (fd < 0) {
			log_error("%s: open failed: %d", path, errno);
			return -1;
		}

		memset(buf, 0, 32);
		snprintf(buf, 32, "%d", id);

		rv = write(fd, buf, strlen(buf));
		if (rv < 0) {
			log_error("%s: write failed: %d, %s", path, errno, buf);
			return -1;
		}

		/* FIXME: remove weight handling from member_cman.c
		   and put here */
	}

	return 0;

}

static int path_exists(const char *path)
{
	struct stat buf;

	if (stat(path, &buf) < 0) {
		if (errno != ENOENT)
			log_error("%s: stat failed: %d", path, errno);
		return 0;
	}
	return 1;
}

int set_id(char *name, uint32_t id)
{
	char path[PATH_MAX];
	char buf[32];
	int rv, fd;

	if (!path_exists("/config/dlm")) {
		log_error("No /config/dlm, is the dlm loaded?");
		return -1;
	}

	create_path("/config/dlm/cluster");

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX, "%s/%s", LS_DIR, name);

	log_debug("set_id mkdir %s", path);

	rv = create_path(path);
	if (rv)
		return -1;

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX, "%s/%s/id", LS_DIR, name);

	fd = open(path, O_WRONLY);
	if (fd < 0) {
		log_error("%s: open failed: %d", path, errno);
		return -1;
	}

	memset(buf, 0, sizeof(buf));
	snprintf(buf, 32, "%u", id);

	rv = write(fd, buf, strlen(buf));
	if (rv < 0) {
		log_error("%s: write failed: %d, %s", path, errno, buf);
		return -1;
	}

	return 0;
}

