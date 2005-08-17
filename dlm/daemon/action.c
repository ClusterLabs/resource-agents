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

#include "dlm_daemon.h"

static int dir_members[MAX_GROUP_MEMBERS];
static int dir_members_count;

#define DLM_SYSFS_DIR "/sys/kernel/dlm"
#define LS_DIR "/config/dlm/cluster/spaces"


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

int set_control(char *name, int val)
{
	char buf[32];

	memset(buf, 0, sizeof(buf));
	snprintf(buf, 32, "%d", val);

	return do_sysfs(name, "control", buf);
}

int set_event_done(char *name, int val)
{
	char buf[32];

	memset(buf, 0, sizeof(buf));
	snprintf(buf, 32, "%d", val);

	return do_sysfs(name, "event_done", buf);
}

int set_id(char *name, uint32_t id)
{
	char buf[32];

	memset(buf, 0, sizeof(buf));
	snprintf(buf, 32, "%u", id);

	return do_sysfs(name, "id", buf);
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
	closedir(d);

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

int set_members(char *name, int new_count, int *new_members)
{
	char path[PATH_MAX];
	char buf[32];
	int i, fd, rv, id, old_count, *old_members;

	/*
	 * create lockspace dir if it doesn't exist yet
	 */

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX, "%s/%s", LS_DIR, name);

	if (!path_exists(path)) {
		if (create_path(path))
			return -1;
	}

	/*
	 * remove/add lockspace members
	 */

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
			close(fd);
			return -1;
		}
		close(fd);

		/* FIXME: remove weight handling from member_cman.c
		   and put here */
	}

	return 0;

}

char *str_ip(char *addr)
{
	static char ip[256];
	struct sockaddr_in *sin = (struct sockaddr_in *) addr;
	memset(ip, 0, sizeof(ip));
	inet_ntop(AF_INET, &sin->sin_addr, ip, 256);
	return ip;
}

int set_node(int nodeid, char *addr, int local)
{
	char path[PATH_MAX];
	char buf[32];
	int rv, fd;

	log_debug("set_node %d %s local %d", nodeid, str_ip(addr), local);

	if (!path_exists("/config/dlm")) {
		log_error("No /config/dlm, is the dlm loaded?");
		return -1;
	}

	if (!path_exists("/config/dlm/cluster"))
		create_path("/config/dlm/cluster");

	/*
	 * create comm dir for this node
	 */

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX, "/config/dlm/cluster/comms/%d", nodeid);

	rv = create_path(path);
	if (rv)
		return -1;

	/*
	 * set the nodeid
	 */

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX, "/config/dlm/cluster/comms/%d/nodeid", nodeid);

	fd = open(path, O_WRONLY);
	if (fd < 0) {
		log_error("%s: open failed: %d", path, errno);
		return -1;
	}

	memset(buf, 0, sizeof(buf));
	snprintf(buf, 32, "%d", nodeid);

	rv = write(fd, buf, strlen(buf));
	if (rv < 0) {
		log_error("%s: write failed: %d, %s", path, errno, buf);
		close(fd);
		return -1;
	}
	close(fd);

	/*
	 * set the address
	 */

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX, "/config/dlm/cluster/comms/%d/addr", nodeid);

	fd = open(path, O_WRONLY);
	if (fd < 0) {
		log_error("%s: open failed: %d", path, errno);
		return -1;
	}

	rv = write(fd, addr, sizeof(struct sockaddr_storage));
	if (rv != sizeof(struct sockaddr_storage)) {
		log_error("%s: write failed: %d %d", path, errno, rv);
		close(fd);
		return -1;
	}
	close(fd);

	/*
	 * set local
	 */

	if (!local)
		goto out;

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX, "/config/dlm/cluster/comms/%d/local", nodeid);

	fd = open(path, O_WRONLY);
	if (fd < 0) {
		log_error("%s: open failed: %d", path, errno);
		return -1;
	}

	rv = write(fd, "1", strlen("1"));
	if (rv < 0) {
		log_error("%s: write failed: %d", path, errno);
		close(fd);
		return -1;
	}
	close(fd);
 out:
	return 0;
}

