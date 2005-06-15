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
members    <ls_name> <nodeid>...
control    <ls_name> <num>
event_done <ls_name> <num>
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

static int ls_general(char *name, char *file, char *val)
{
	char fname[512];
	int rv, fd, len;

	sprintf(fname, "%s/%s/%s", DLM_SYSFS_DIR, name, file);

	fd = open(fname, O_WRONLY);
	if (fd < 0) {
		printf("open \"%s\" error %d %d\n", fname, fd, errno);
		return -1;
	}

	/* printf("write \"%s\" to \"%s\"\n", val, fname); */

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

int ls_control(int argc, char **argv)
{
	return ls_general(argv[0], "control", argv[1]);
}

int ls_event_done(int argc, char **argv)
{
	return ls_general(argv[0], "event_done", argv[1]);
}

int ls_members(int argc, char **argv)
{
	return ls_general(argv[0], "members", argv[1]);
}

int ls_set_id(int argc, char **argv)
{
	return ls_general(argv[0], "id", argv[1]);
}

