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

#include "dlm_node.h"

#define DLM_SYSFS_DIR "/sys/kernel/dlm"


int do_command(int op, struct dlm_node_ioctl *ni);

/*
set_local  <nodeid> <ipaddr> [<weight>]
set_node   <nodeid> <ipaddr> [<weight>]
stop       <ls_name>
terminate  <ls_name>
start      <ls_name> <event_nr> <type> <nodeid>...
finish     <ls_name> <event_nr>
poll_done  <ls_name> <event_nr>
set_id     <ls_name> <id>
*/


/*
 * ioctl interface only used for setting up addr/nodeid info
 * with set_local and set_node
 */

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

/*
 * sysfs interface used for lockspace control (stop/start/finish/terminate),
 * for setting lockspace id, lockspace members
 */

int ls_stop(int argc, char **argv)
{
	char fname[512];
	int rv, fd;

	if (argc != 1)
		return -EINVAL;

	sprintf(fname, "%s/%s/stop", DLM_SYSFS_DIR, argv[0]);

	fd = open(fname, O_RDWR);
	if (fd < 0) {
		printf("open error %d %d\n", fd, errno);
		return -1;
	}

	rv = write(fd, "1", strlen("1"));
	if (rv != 1)
		printf("write error %d %d\n", rv, errno);

	close(fd);
	return 0;
}

int ls_terminate(int argc, char **argv)
{
	char fname[512];
	int rv, fd;

	if (argc != 1)
		return -EINVAL;

	sprintf(fname, "%s/%s/terminate", DLM_SYSFS_DIR, argv[0]);

	fd = open(fname, O_RDWR);
	if (fd < 0) {
		printf("open error %d %d\n", fd, errno);
		return -1;
	}

	rv = write(fd, "1", strlen("1"));
	if (rv != 1)
		printf("write error %d %d\n", rv, errno);

	close(fd);
	return 0;
}

int ls_finish(int argc, char **argv)
{
	char fname[512];
	int rv, fd;

	if (argc != 2)
		return -EINVAL;

	sprintf(fname, "%s/%s/finish", DLM_SYSFS_DIR, argv[0]);

	fd = open(fname, O_RDWR);
	if (fd < 0) {
		printf("open error %d %d\n", fd, errno);
		return -1;
	}

	rv = write(fd, argv[1], strlen(argv[1]));
	if (rv != strlen(argv[1]))
		printf("write error %d %d\n", rv, errno);

	close(fd);
	return 0;
}

int ls_start(int argc, char **argv)
{
	char fname[512];
	int i, rv, fd, len = 0;
	char *p;

	if (argc < 4)
		return -EINVAL;

	/* first set up new members */

	for (i = 3; i < argc; i++)
		len += strlen(argv[i]) + 1;

	p = malloc(len);
	if (!p) {
		printf("malloc error\n");
		return -ENOMEM;
	}
	memset(p, 0, len);

	for (i = 3; i < argc; i++) {
		if (i != 3)
			strcat(p, " ");
		strcat(p, argv[i]);
	}

	sprintf(fname, "%s/%s/members", DLM_SYSFS_DIR, argv[0]);

	fd = open(fname, O_RDWR);
	if (fd < 0) {
		printf("open error %s %d %d\n", fname, fd, errno);
		return -1;
	}

	rv = write(fd, p, len);
	if (rv != len) {
		printf("write error %s %d %d\n", fname, rv, errno);
		close(fd);
		return -1;
	}

	free(p);
	close(fd);

	/* second do the start */

	sprintf(fname, "%s/%s/start", DLM_SYSFS_DIR, argv[0]);

	fd = open(fname, O_RDWR);
	if (fd < 0) {
		printf("open error %s %d %d\n", fname, fd, errno);
		return -1;
	}

	len = strlen(argv[1]);
	rv = write(fd, argv[1], len);
	if (rv != len)
		printf("write error %s %d %d\n", fname, rv, errno);

	close(fd);
	return 0;
}

int ls_set_id(int argc, char **argv)
{
	char fname[512];
	int len, fd, rv;

	if (argc != 2)
		return -EINVAL;

	sprintf(fname, "%s/%s/id", DLM_SYSFS_DIR, argv[0]);

	fd = open(fname, O_RDWR);
	if (fd < 0) {
		printf("open error %d %d\n", fd, errno);
		return -1;
	}

	len = strlen(argv[1]);
	rv = write(fd, argv[1], len);
	if (rv != len)
		printf("write error %d %d\n", rv, errno);

	close(fd);
	return 0;
}

int ls_get_done(int argc, char **argv, int *event_nr)
{
	char fname[512];
	char buf[32];
	int fd, rv;

	if (argc != 1)
		return -EINVAL;

	sprintf(fname, "%s/%s/done", DLM_SYSFS_DIR, argv[0]);

	fd = open(fname, O_RDONLY);
	if (fd < 0) {
		printf("open error %d %d\n", fd, errno);
		return -1;
	}

	memset(buf, 0, 32);

	rv = read(fd, buf, 32);
	if (rv <= 0) {
		printf("read error %s %d %d\n", fname, rv, errno);
		goto out;
	}

	*event_nr = atoi(buf);
 out:
	close(fd);
	return 0;
}

int ls_poll_done(int argc, char **argv)
{
	/* FIXME: loop reading /sys/kernel/dlm/<ls>/done until it
	   equals the given event_nr */
	printf("not yet implemented\n");
	return -1;
}

