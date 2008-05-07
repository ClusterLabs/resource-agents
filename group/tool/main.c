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
#include <sys/un.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>

#include "libgroup.h"
#include "groupd.h"

#define MAX_GROUPS			64
#define OPTION_STRING			"hVv"

/* copied from cluster/group/gfs_controld/lock_dlm.h */
#define LOCK_DLM_SOCK_PATH		"gfs_controld_sock"

/* copied from cluster/fence/fenced/fd.h */
#define FENCED_SOCK_PATH		"fenced_socket"

/* needs to match the same in cluster/group/daemon/gd_internal.h and
   cluster/group/gfs_controld/lock_dlm.h and cluster/fence/fenced/fd.h */
#define DUMP_SIZE			(1024 * 1024)

/* needs to match the same in cluster/group/gfs_controld/lock_dlm.h,
   it's the message size that gfs_controld takes */
#define MAXLINE				256

#define OP_LS				1
#define OP_DUMP				2
#define OP_LOG				3

static char *prog_name;
static int operation;
static int opt_ind;
static int verbose;

static int do_write(int fd, void *buf, size_t count)
{
	int rv, off = 0;

 retry:
	rv = write(fd, buf + off, count);
	if (rv == -1 && errno == EINTR)
		goto retry;
	if (rv < 0)
		return rv;

	if (rv != count) {
		count -= rv;
		off += rv;
		goto retry;
	}
	return 0;
}

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

static void print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s [options] [ls|dump]\n", prog_name);
	printf("\n");
	printf("Options:\n");
	printf("  -v               Verbose output, extra event information\n");
	printf("  -h               Print this help, then exit\n");
	printf("  -V               Print program version information, then exit\n");
	printf("\n");
	printf("Display group information from groupd\n");
	printf("ls                 Show information for all groups\n");
	printf("ls <level> <name>  Show information one group\n");
	printf("\n");
	printf("Display debugging information\n");
	printf("dump               Show debug log from groupd\n");
	printf("dump fence         Show debug log from fenced\n");
	printf("dump gfs           Show debug log from gfs_controld\n");
	printf("dump plocks <name> Show posix locks for gfs with given name\n");
	printf("\n");
	printf("log <comments>     Add information to the groupd log.\n");
	printf("\n");
}

static void decode_arguments(int argc, char **argv)
{
	int cont = 1;
	int optchar;

	while (cont) {
		optchar = getopt(argc, argv, OPTION_STRING);

		switch (optchar) {
		case 'v':
			verbose = 1;
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case 'V':
			printf("%s (built %s %s)\n",
				prog_name, __DATE__, __TIME__);
			/* printf("%s\n", REDHAT_COPYRIGHT); */
			exit(EXIT_SUCCESS);
			break;

		case ':':
		case '?':
			fprintf(stderr, "Please use '-h' for usage.\n");
			exit(EXIT_FAILURE);
			break;

		case EOF:
			cont = 0;
			break;

		default:
			fprintf(stderr, "unknown option: %c\n", optchar);
			exit(EXIT_FAILURE);
			break;
		};
	}

	while (optind < argc) {
		if (strcmp(argv[optind], "dump") == 0) {
			operation = OP_DUMP;
			opt_ind = optind + 1;
			break;
		} else if (strcmp(argv[optind], "ls") == 0 ||
		           strcmp(argv[optind], "list") == 0) {
			operation = OP_LS;
			opt_ind = optind + 1;
			break;
		} else if (strcmp(argv[optind], "log") == 0) {
			operation = OP_LOG;
			opt_ind = optind + 1;
			break;
		}
		optind++;
	}

	if (!operation)
		operation = OP_LS;
}

/* copied from grouip/daemon/gd_internal.h, must keep in sync */

#define EST_JOIN_BEGIN         1
#define EST_JOIN_STOP_WAIT     2
#define EST_JOIN_ALL_STOPPED   3
#define EST_JOIN_START_WAIT    4
#define EST_JOIN_ALL_STARTED   5
#define EST_LEAVE_BEGIN        6
#define EST_LEAVE_STOP_WAIT    7
#define EST_LEAVE_ALL_STOPPED  8
#define EST_LEAVE_START_WAIT   9
#define EST_LEAVE_ALL_STARTED 10
#define EST_FAIL_BEGIN        11
#define EST_FAIL_STOP_WAIT    12
#define EST_FAIL_ALL_STOPPED  13
#define EST_FAIL_START_WAIT   14
#define EST_FAIL_ALL_STARTED  15

char *ev_state_str(state)
{
	switch (state) {
	case EST_JOIN_BEGIN:
		return "JOIN_BEGIN";
	case EST_JOIN_STOP_WAIT:
		return "JOIN_STOP_WAIT";
	case EST_JOIN_ALL_STOPPED:
		return "JOIN_ALL_STOPPED";
	case EST_JOIN_START_WAIT:
		return "JOIN_START_WAIT";
	case EST_JOIN_ALL_STARTED:
		return "JOIN_ALL_STARTED";
	case EST_LEAVE_BEGIN:
		return "LEAVE_BEGIN";
	case EST_LEAVE_STOP_WAIT:
		return "LEAVE_STOP_WAIT";
	case EST_LEAVE_ALL_STOPPED:
		return "LEAVE_ALL_STOPPED";
	case EST_LEAVE_START_WAIT:
		return "LEAVE_START_WAIT";
	case EST_LEAVE_ALL_STARTED:
		return "LEAVE_ALL_STARTED";
	case EST_FAIL_BEGIN:
		return "FAIL_BEGIN";
	case EST_FAIL_STOP_WAIT:
		return "FAIL_STOP_WAIT";
	case EST_FAIL_ALL_STOPPED:
		return "FAIL_ALL_STOPPED";
	case EST_FAIL_START_WAIT:
		return "FAIL_START_WAIT";
	case EST_FAIL_ALL_STARTED:
		return "FAIL_ALL_STARTED";
	default:
		return "unknown";
	}
}

char *state_str(group_data_t *data)
{
	static char buf[128];
	
	memset(buf, 0, sizeof(buf));

	if (!data->event_state && !data->event_nodeid)
		sprintf(buf, "none");
	else if (verbose)
		snprintf(buf, 127, "%s %d %llx %d",
			 ev_state_str(data->event_state),
			 data->event_nodeid,
			 (unsigned long long)data->event_id,
			 data->event_local_status);
	else
		snprintf(buf, 127, "%s", ev_state_str(data->event_state));

	return buf;
}

static int data_compare(const void *va, const void *vb)
{
	const group_data_t *a = va;
	const group_data_t *b = vb;
	return a->level - b->level;
}

static int member_compare(const void *va, const void *vb)
{
	const int *a = va;
	const int *b = vb;
	return *a - *b;
}

int do_ls(int argc, char **argv)
{
	group_data_t data[MAX_GROUPS];
	int i, j, rv, count = 0, level;
	char *name, *state_header;
	int type_width = 16;
	int level_width = 5;
	int name_width = 32;
	int id_width = 8;
	int state_width = 12;
	int len, max_name = 4;

	memset(&data, 0, sizeof(data));

	if (opt_ind && opt_ind < argc) {
		level = atoi(argv[opt_ind++]);
		name = argv[opt_ind];

		rv = group_get_group(level, name, data);
		count = 1;
	} else
		rv = group_get_groups(MAX_GROUPS, &count, data);

	if (rv < 0) {
		fprintf(stderr,"Unable to connect to groupd.  Is it running?\n");
		return rv;
	}
	for (i = 0; i < count; i++) {
		len = strlen(data[i].name);
		if (len > max_name)
			max_name = len;
	}
	name_width = max_name + 1;

	if (verbose)
		state_header = "state node id local_done";
	else
		state_header = "state";
			
	printf("%-*s %-*s %-*s %-*s %-*s\n",
		type_width, "type",
		level_width, "level",
		name_width, "name",
		id_width, "id",
		state_width, state_header);

	qsort(&data, count, sizeof(group_data_t), data_compare);

	for (i = 0; i < count; i++) {

		printf("%-*s %-*d %-*s %0*x %-*s\n",
			type_width, data[i].client_name,
			level_width, data[i].level,
			name_width, data[i].name,
			id_width, data[i].id,
			state_width, state_str(&data[i]));

		qsort(&data[i].members, data[i].member_count,
		      sizeof(int), member_compare);

		printf("[");
		for (j = 0; j < data[i].member_count; j++) {
			if (j != 0)
				printf(" ");
			printf("%d", data[i].members[j]);
		}
		printf("]\n");
	}
	return 0;
}

static int connect_daemon(char *path)
{
	struct sockaddr_un sun;
	socklen_t addrlen;
	int rv, fd;

	fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		goto out;

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strcpy(&sun.sun_path[1], path);
	addrlen = sizeof(sa_family_t) + strlen(sun.sun_path+1) + 1;

	rv = connect(fd, (struct sockaddr *) &sun, addrlen);
	if (rv < 0) {
		close(fd);
		fd = rv;
	}
 out:
	return fd;
}

int do_dump(int argc, char **argv, int fd)
{
	char inbuf[DUMP_SIZE];
	char outbuf[GROUPD_MSGLEN];
	int rv;

	memset(inbuf, 0, sizeof(inbuf));
	memset(outbuf, 0, sizeof(outbuf));

	sprintf(outbuf, "dump");

	rv = do_write(fd, outbuf, sizeof(outbuf));
	if (rv < 0) {
		printf("dump write error %d errno %d\n", rv, errno);
		return -1;
	}

	do_read(fd, inbuf, sizeof(inbuf));
	do_write(STDOUT_FILENO, inbuf, strlen(inbuf));

	close(fd);
	return 0;
}

int do_maxline_dump(int argc, char **argv, int fd)
{
	char inbuf[DUMP_SIZE];
	char outbuf[MAXLINE];
	int rv;

	memset(inbuf, 0, sizeof(inbuf));
	memset(outbuf, 0, sizeof(outbuf));

	sprintf(outbuf, "dump");

	rv = do_write(fd, outbuf, sizeof(outbuf));
	if (rv < 0) {
		printf("dump write error %d errno %d\n", rv, errno);
		return -1;
	}

	do_read(fd, inbuf, sizeof(inbuf));
	do_write(STDOUT_FILENO, inbuf, sizeof(inbuf));

	close(fd);
	return 0;
}

int do_plock_dump(int argc, char **argv, int fd)
{
	char inbuf[MAXLINE];
	char outbuf[MAXLINE];
	int rv;

	memset(outbuf, 0, sizeof(outbuf));

	if (opt_ind + 1 >= argc) {
		printf("plocks option requires a group name\n");
		return -1;
	}

	sprintf(outbuf, "plocks %s", argv[opt_ind + 1]);

	rv = do_write(fd, outbuf, sizeof(outbuf));
	if (rv < 0) {
		printf("dump write error %d errno %d\n", rv, errno);
		return -1;
	}

	while (1) {
		memset(&inbuf, 0, sizeof(inbuf));
		rv = read(fd, inbuf, sizeof(inbuf));
		if (rv <= 0)
			break;
		rv = write(STDOUT_FILENO, inbuf, rv);
		if (rv < 0) {
			printf("dump write error %d errno %d\n", rv, errno);
			return  -1;
		}
	}

	close(fd);
	return 0;
}

int do_log(char *comment)
{
	char buf[GROUPD_MSGLEN];
	int fd, rv;

	fd = connect_daemon(GROUPD_SOCK_PATH);
	if (fd < 0)
		return fd;
	memset(buf, 0, sizeof(buf));
	snprintf(buf, sizeof(buf), "log %s", comment);
	rv = write(fd, &buf, GROUPD_MSGLEN);
	close(fd);
	return rv;
}

int main(int argc, char **argv)
{
	int fd;

	prog_name = argv[0];
	decode_arguments(argc, argv);

	switch (operation) {
	case OP_LS:
		return do_ls(argc, argv);

	case OP_DUMP:
		if (opt_ind && opt_ind < argc) {
			if (!strncmp(argv[opt_ind], "gfs", 3)) {
				fd = connect_daemon(LOCK_DLM_SOCK_PATH);
				if (fd < 0)
					return -1;
				return do_maxline_dump(argc, argv, fd);
			}

			if (!strncmp(argv[opt_ind], "fence", 5)) {
				fd = connect_daemon(FENCED_SOCK_PATH);
				if (fd < 0)
					return -1;
				return do_maxline_dump(argc, argv, fd);
			}

			if (!strncmp(argv[opt_ind], "plocks", 5)) {
				fd = connect_daemon(LOCK_DLM_SOCK_PATH);
				if (fd < 0)
					return -1;
				return do_plock_dump(argc, argv, fd);
			}
		}

		fd = connect_daemon(GROUPD_SOCK_PATH);
		if (fd < 0)
			break;
		return do_dump(argc, argv, fd);

	case OP_LOG:
		if (opt_ind && opt_ind < argc) {
			return do_log(argv[opt_ind]);
		}
	}

	return 0;
}

