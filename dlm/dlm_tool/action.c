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

#include "dlm_member.h"

#define DLM_SYSFS_DIR "/sys/kernel/dlm"


int do_command(struct dlm_member_ioctl *mi);

/*
set_local  <nodeid> <ipaddr> [<weight>]
set_node   <nodeid> <ipaddr> [<weight>]
stop       <ls_name>
terminate  <ls_name>
start      <ls_name> <event_nr> <nodeid>...
finish     <ls_name> <event_nr>
poll_done  <ls_name> <event_nr>
set_id     <ls_name> <id>
*/


/*
 * ioctl interface only used for setting up addr/nodeid info
 * with set_local and set_node
 */

static void init_mi(struct dlm_member_ioctl *mi)
{
	memset(mi, 0, sizeof(struct dlm_member_ioctl));

	mi->version[0] = DLM_MEMBER_VERSION_MAJOR;
	mi->version[1] = DLM_MEMBER_VERSION_MINOR;
	mi->version[2] = DLM_MEMBER_VERSION_PATCH;

	mi->data_size = sizeof(struct dlm_member_ioctl);
	mi->data_start = sizeof(struct dlm_member_ioctl);
}

static void set_ipaddr(struct dlm_member_ioctl *mi, char *ip)
{
	struct sockaddr_in sin;
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	inet_pton(AF_INET, ip, &sin.sin_addr);
	memcpy(mi->addr, &sin, sizeof(sin));
}

int set_node(int argc, char **argv)
{
	struct dlm_member_ioctl mi;

	if (argc < 2 || argc > 3)
		return -EINVAL;

	init_mi(&mi);
	strcpy(mi.op, "set_node");
	mi.nodeid = atoi(argv[0]);
	set_ipaddr(&mi, argv[1]);
	if (argc > 2)
		mi.weight = atoi(argv[2]);
	return do_command(&mi);
}

int set_local(int argc, char **argv)
{
	struct dlm_member_ioctl mi;

	if (argc < 2 || argc > 3)
		return -EINVAL;

	init_mi(&mi);
	strcpy(mi.op, "set_local");
	mi.nodeid = atoi(argv[0]);
	set_ipaddr(&mi, argv[1]);
	if (argc > 2)
		mi.weight = atoi(argv[2]);
	return do_command(&mi);
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
	if (rv != 1) {
		printf("write error %d %d\n", rv, errno);
		return -1;
	}

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
	if (rv != 1) {
		printf("write error %d %d\n", rv, errno);
		return -1;
	}

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
	if (rv != strlen(argv[1])) {
		printf("write error %d %d\n", rv, errno);
		return -1;
	}

	return 0;
}

int ls_start(int argc, char **argv)
{
	char fname[512];
	int i, rv, fd, len = 0;
	char *p;

	if (argc < 3)
		return -EINVAL;

	/* first set up new members */

	for (i = 2; i < argc; i++)
		len += strlen(argv[i]) + 1;
	len -= 1;

	p = malloc(len);
	if (!p) {
		printf("malloc error\n");
		return -ENOMEM;
	}
	memset(p, 0, len);

	for (i = 2; i < argc; i++) {
		if (i != 2)
			strcat(p, " ");
		strcat(p, argv[i]);
	}

	sprintf(fname, "%s/%s/members", DLM_SYSFS_DIR, argv[0]);

	fd = open(fname, O_RDWR);
	if (fd < 0) {
		printf("open error %s %d %d\n", fname, fd, errno);
		return -1;
	}

	printf("write to %s %d: \"%s\"\n", fname, len, p);
	rv = write(fd, p, len);
	if (rv != len) {
		printf("write error %s %d %d\n", fname, rv, errno);
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

	printf("write to %s: \"%s\"\n", fname, argv[1]);
	len = strlen(argv[1]);
	rv = write(fd, argv[1], len);
	if (rv != len) {
		printf("write error %s %d %d\n", fname, rv, errno);
		return -1;
	}

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
	if (rv != len) {
		printf("write error %d %d\n", rv, errno);
		return -1;
	}

	return 0;
}

int ls_poll_done(int argc, char **argv)
{
	/* FIXME: loop reading /sys/kernel/dlm/<ls>/done until it
	   equals the given event_nr */
	printf("not yet implemented\n");
	return -1;
}

