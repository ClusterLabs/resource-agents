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
#include "ccs.h"

static int dir_members[MAX_GROUP_MEMBERS];
static int dir_members_count;
static int comms_nodes[MAX_NODES];
static int comms_nodes_count;

#define DLM_SYSFS_DIR "/sys/kernel/dlm"
#define SPACES_DIR    "/sys/kernel/config/dlm/cluster/spaces"
#define COMMS_DIR     "/sys/kernel/config/dlm/cluster/comms"


static int do_write(int fd, void *buf, size_t count)
{
	int rv, off = 0;

 retry:
	rv = write(fd, buf + off, count);
	if (rv == -1 && errno == EINTR)
		goto retry;
	if (rv < 0) {
		log_error("write errno %d", errno);
		return rv;
	}

	if (rv != count) {
		count -= rv;
		off += rv;
		goto retry;
	}
	return 0;
}

static int do_sysfs(char *name, char *file, char *val)
{
	char fname[512];
	int rv, fd;

	sprintf(fname, "%s/%s/%s", DLM_SYSFS_DIR, name, file);

	fd = open(fname, O_WRONLY);
	if (fd < 0) {
		log_error("open \"%s\" error %d %d", fname, fd, errno);
		return -1;
	}

	log_debug("write \"%s\" to \"%s\"", val, fname);

	rv = do_write(fd, val, strlen(val) + 1);
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
	snprintf(path, PATH_MAX, "%s/%s/nodes", SPACES_DIR, name);

	d = opendir(path);
	if (!d) {
		log_debug("%s: opendir failed: %d", path, errno);
		return -1;
	}

	memset(dir_members, 0, sizeof(dir_members));
	dir_members_count = 0;

	/* FIXME: we should probably read the nodeid in each dir instead */

	while ((de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;
		dir_members[i++] = atoi(de->d_name);
		log_debug("dir_member %d", dir_members[i-1]);
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

static int open_ccs(void)
{
	int i, cd;

	while ((cd = ccs_connect()) < 0) {
		sleep(1);
		if (++i > 9 && !(i % 10))
			log_error("connect to ccs error %d, "
				  "check ccsd or cluster status", cd);
	}
	return cd;
}

/* when not set in cluster.conf, a node's default weight is 1 */

#define WEIGHT_PATH "/cluster/clusternodes/clusternode[@name=\"%s\"]/@weight"

static int get_weight(int cd, int nodeid)
{
	char path[PATH_MAX], *str, *name;
	int error, w;

	name = nodeid2name(nodeid);
	if (!name) {
		log_error("no name for nodeid %d", nodeid);
		return 1;
	}

	memset(path, 0, PATH_MAX);
	sprintf(path, WEIGHT_PATH, name);

	error = ccs_get(cd, path, &str);
	if (error || !str)
		return 1;

	w = atoi(str);
	free(str);
	return w;
}

int set_members(char *name, int new_count, int *new_members)
{
	char path[PATH_MAX];
	char buf[32];
	int i, w, fd, rv, id, cd = 0, old_count, *old_members;

	/*
	 * create lockspace dir if it doesn't exist yet
	 */

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX, "%s/%s", SPACES_DIR, name);

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
		snprintf(path, PATH_MAX, "%s/%s/nodes/%d",
			 SPACES_DIR, name, id);

		log_debug("set_members rmdir \"%s\"", path);

		rv = rmdir(path);
		if (rv) {
			log_error("%s: rmdir failed: %d", path, errno);
			goto out;
		}
	}

	/*
	 * remove lockspace dir after we've removed all the nodes
	 * (when we're shutting down and adding no new nodes)
	 */

	if (!new_count) {
		memset(path, 0, PATH_MAX);
		snprintf(path, PATH_MAX, "%s/%s", SPACES_DIR, name);

		log_debug("set_members lockspace rmdir \"%s\"", path);

		rv = rmdir(path);
		if (rv)
			log_error("%s: rmdir failed: %d", path, errno);
	}

	for (i = 0; i < new_count; i++) {
		id = new_members[i];
		if (id_exists(id, old_count, old_members))
			continue;

		if (!is_cman_member(id))
			cman_statechange();
		/*
		 * create node's dir
		 */

		memset(path, 0, PATH_MAX);
		snprintf(path, PATH_MAX, "%s/%s/nodes/%d",
			 SPACES_DIR, name, id);

		log_debug("set_members mkdir \"%s\"", path);

		rv = create_path(path);
		if (rv)
			goto out;

		/*
		 * set node's nodeid
		 */

		memset(path, 0, PATH_MAX);
		snprintf(path, PATH_MAX, "%s/%s/nodes/%d/nodeid",
			 SPACES_DIR, name, id);

		rv = fd = open(path, O_WRONLY);
		if (rv < 0) {
			log_error("%s: open failed: %d", path, errno);
			goto out;
		}

		memset(buf, 0, 32);
		snprintf(buf, 32, "%d", id);

		rv = do_write(fd, buf, strlen(buf));
		if (rv < 0) {
			log_error("%s: write failed: %d, %s", path, errno, buf);
			close(fd);
			goto out;
		}
		close(fd);

		/*
		 * set node's weight
		 */

		if (!cd)
			cd = open_ccs();

		w = get_weight(cd, id);

		memset(path, 0, PATH_MAX);
		snprintf(path, PATH_MAX, "%s/%s/nodes/%d/weight",
			 SPACES_DIR, name, id);

		rv = fd = open(path, O_WRONLY);
		if (rv < 0) {
			log_error("%s: open failed: %d", path, errno);
			goto out;
		}

		memset(buf, 0, 32);
		snprintf(buf, 32, "%d", w);

		rv = do_write(fd, buf, strlen(buf));
		if (rv < 0) {
			log_error("%s: write failed: %d, %s", path, errno, buf);
			close(fd);
			goto out;
		}
		close(fd);
	}

	rv = 0;
 out:
	if (cd)
		ccs_disconnect(cd);
	return rv;
}

#if 0
char *str_ip(char *addr)
{
	static char ip[256];
	struct sockaddr_in *sin = (struct sockaddr_in *) addr;
	memset(ip, 0, sizeof(ip));
	inet_ntop(AF_INET, &sin->sin_addr, ip, 256);
	return ip;
}
#endif

char *str_ip(char *addr)
{
	static char str_ip_buf[INET6_ADDRSTRLEN];
	struct sockaddr_storage *ss = (struct sockaddr_storage *)addr;
	struct sockaddr_in *sin = (struct sockaddr_in *)addr;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)addr;
	void *saddr;

	if (ss->ss_family == AF_INET6)
		saddr = &sin6->sin6_addr;
	else
		saddr = &sin->sin_addr;

	inet_ntop(ss->ss_family, saddr, str_ip_buf, sizeof(str_ip_buf));
	return str_ip_buf;
}

/* record the nodeids that are currently listed under
   config/dlm/cluster/comms/ so that we can remove all of them */

static int update_comms_nodes(void)
{
	char path[PATH_MAX];
	DIR *d;
	struct dirent *de;
	int i = 0;

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX, COMMS_DIR);

	d = opendir(path);
	if (!d) {
		log_debug("%s: opendir failed: %d", path, errno);
		return -1;
	}

	memset(comms_nodes, 0, sizeof(comms_nodes));
	comms_nodes_count = 0;

	while ((de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;
		comms_nodes[i++] = atoi(de->d_name);
	}
	closedir(d);

	comms_nodes_count = i;
	return 0;
}

/* clear out everything under config/dlm/cluster/comms/ */

void clear_configfs_comms(void)
{
	char path[PATH_MAX];
	int i, rv;

	rv = update_comms_nodes();
	if (rv < 0)
		return;

	for (i = 0; i < comms_nodes_count; i++) {
		memset(path, 0, PATH_MAX);
		snprintf(path, PATH_MAX, "%s/%d", COMMS_DIR, comms_nodes[i]);

		log_debug("clear_configfs_nodes rmdir \"%s\"", path);

		rv = rmdir(path);
		if (rv)
			log_error("%s: rmdir failed: %d", path, errno);
	}
}

static void clear_configfs_space_nodes(char *name)
{
	char path[PATH_MAX];
	int i, rv;

	rv = update_dir_members(name);
	if (rv < 0)
		return;

	for (i = 0; i < dir_members_count; i++) {
		memset(path, 0, PATH_MAX);
		snprintf(path, PATH_MAX, "%s/%s/nodes/%d",
			 SPACES_DIR, name, dir_members[i]);

		log_debug("clear_configfs_space_nodes rmdir \"%s\"", path);

		rv = rmdir(path);
		if (rv)
			log_error("%s: rmdir failed: %d", path, errno);
	}
}

/* clear out everything under config/dlm/cluster/spaces/ */

void clear_configfs_spaces(void)
{
	char path[PATH_MAX];
	DIR *d;
	struct dirent *de;
	int rv;

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX, "%s", SPACES_DIR);

	d = opendir(path);
	if (!d) {
		log_debug("%s: opendir failed: %d", path, errno);
		return;
	}

	while ((de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;

		clear_configfs_space_nodes(de->d_name);

		memset(path, 0, PATH_MAX);
		snprintf(path, PATH_MAX, "%s/%s", SPACES_DIR, de->d_name);
		
		log_debug("clear_configfs_spaces rmdir \"%s\"", path);

		rv = rmdir(path);
		if (rv)
			log_error("%s: rmdir failed: %d", path, errno);
	}
	closedir(d);
}

void clear_configfs(void)
{
	clear_configfs_comms();
	clear_configfs_spaces();
	rmdir("/sys/kernel/config/dlm/cluster");
}

int add_configfs_node(int nodeid, char *addr, int addrlen, int local)
{
	char path[PATH_MAX];
	char padded_addr[sizeof(struct sockaddr_storage)];
	char buf[32];
	int rv, fd;

	log_debug("set_configfs_node %d %s local %d",
		  nodeid, str_ip(addr), local);

	if (!path_exists("/sys/kernel/config")) {
		log_error("No /sys/kernel/config, is configfs loaded?");
		return -1;
	}

	if (!path_exists("/sys/kernel/config/dlm")) {
		log_error("No /sys/kernel/config/dlm, is the dlm loaded?");
		return -1;
	}

	if (!path_exists("/sys/kernel/config/dlm/cluster"))
		create_path("/sys/kernel/config/dlm/cluster");

	/*
	 * create comm dir for this node
	 */

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX, "%s/%d", COMMS_DIR, nodeid);

	rv = create_path(path);
	if (rv)
		return -1;

	/*
	 * set the nodeid
	 */

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX, "%s/%d/nodeid", COMMS_DIR, nodeid);

	fd = open(path, O_WRONLY);
	if (fd < 0) {
		log_error("%s: open failed: %d", path, errno);
		return -1;
	}

	memset(buf, 0, sizeof(buf));
	snprintf(buf, 32, "%d", nodeid);

	rv = do_write(fd, buf, strlen(buf));
	if (rv < 0) {
		log_error("%s: write failed: %d, %s", path, errno, buf);
		close(fd);
		return -1;
	}
	close(fd);

	/*
	 * set the address
	 */

	memset(padded_addr, 0, sizeof(padded_addr));
	memcpy(padded_addr, addr, addrlen);

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX, "%s/%d/addr", COMMS_DIR, nodeid);

	fd = open(path, O_WRONLY);
	if (fd < 0) {
		log_error("%s: open failed: %d", path, errno);
		return -1;
	}

	rv = do_write(fd, padded_addr, sizeof(struct sockaddr_storage));
	if (rv < 0) {
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
	snprintf(path, PATH_MAX, "%s/%d/local", COMMS_DIR, nodeid);

	fd = open(path, O_WRONLY);
	if (fd < 0) {
		log_error("%s: open failed: %d", path, errno);
		return -1;
	}

	rv = do_write(fd, "1", strlen("1"));
	if (rv < 0) {
		log_error("%s: write failed: %d", path, errno);
		close(fd);
		return -1;
	}
	close(fd);
 out:
	return 0;
}

void del_configfs_node(int nodeid)
{
	char path[PATH_MAX];
	int rv;

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX, "%s/%d", COMMS_DIR, nodeid);

	log_debug("del_configfs_node rmdir \"%s\"", path);

	rv = rmdir(path);
	if (rv)
		log_error("%s: rmdir failed: %d", path, errno);
}

