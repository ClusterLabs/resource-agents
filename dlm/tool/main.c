/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2007-2008 Red Hat, Inc.  All rights reserved.
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
#include <limits.h>
#include <netinet/in.h>

#include <linux/dlmconstants.h>
#include "libdlm.h"
#include "libdlmcontrol.h"

#define LKM_IVMODE -1

#define OPTION_STRING			"MhVvd:m:"

#define OP_JOIN				1
#define OP_LEAVE			2
#define OP_JOINLEAVE			3
#define OP_SPACES			4
#define OP_LOCKDUMP			5
#define OP_LOCKDEBUG			6
#define OP_DEADLOCK_CHECK		7

static char *prog_name;
static char *lsname;
static int operation;
static int opt_ind;
static int verbose;
static int opt_dir = 0;
static int dump_mstcpy = 0;
static mode_t create_mode = 0600;

static void print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s [options] [join|leave|lockdump|lockdebug|deadlock_check]\n", prog_name);
	printf("\n");
	printf("Options:\n");
	printf("  -v               Verbose output\n");
	printf("  -d <n>           Resource directory off/on (0/1), default 0\n");
	printf("  -m <mode>        Permission mode for lockspace device (octal), default 0600\n");
	printf("  -M               Print MSTCPY locks in lockdump (remote locks, locally mastered)\n");
	printf("  -h               Print this help, then exit\n");
	printf("  -V               Print program version information, then exit\n");
	printf("\n");
}

static void decode_arguments(int argc, char **argv)
{
	int cont = 1;
	int optchar;
	int need_lsname = 1;
	char modebuf[8];

	while (cont) {
		optchar = getopt(argc, argv, OPTION_STRING);

		switch (optchar) {
		case 'm':
			memset(modebuf, 0, sizeof(modebuf));
			snprintf(modebuf, 8, "%s", optarg);
			sscanf(modebuf, "%o", &create_mode);
			break;

		case 'M':
			dump_mstcpy = 1;
			break;

		case 'v':
			verbose = 1;
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case 'd':
			opt_dir = atoi(optarg);
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
		if (!strncmp(argv[optind], "join", 4) &&
		    (strlen(argv[optind]) == 4)) {
			operation = OP_JOIN;
			opt_ind = optind + 1;
			break;
		} else if (!strncmp(argv[optind], "leave", 5) &&
			   (strlen(argv[optind]) == 5)) {
			operation = OP_LEAVE;
			opt_ind = optind + 1;
			break;
		} else if (!strncmp(argv[optind], "joinleave", 9) &&
			   (strlen(argv[optind]) == 9)) {
			operation = OP_JOINLEAVE;
			opt_ind = optind + 1;
			break;
		} else if (!strncmp(argv[optind], "lockdump", 8) &&
			   (strlen(argv[optind]) == 8)) {
			operation = OP_LOCKDUMP;
			opt_ind = optind + 1;
			break;
		} else if (!strncmp(argv[optind], "lockdebug", 9) &&
			   (strlen(argv[optind]) == 9)) {
			operation = OP_LOCKDEBUG;
			opt_ind = optind + 1;
			break;
		} else if (!strncmp(argv[optind], "spaces", 9) &&
			   (strlen(argv[optind]) == 6)) {
			operation = OP_SPACES;
			opt_ind = optind + 1;
			need_lsname = 0;
			break;
		} else if (!strncmp(argv[optind], "deadlock_check", 14) &&
			   (strlen(argv[optind]) == 14)) {
			operation = OP_DEADLOCK_CHECK;
			opt_ind = optind + 1;
			break;
		}
		optind++;
	}

	if (!operation || !opt_ind) {
		print_usage();
		exit(EXIT_FAILURE);
	}

	if (optind < argc - 1)
		lsname = argv[opt_ind];
	else if (need_lsname) {
		fprintf(stderr, "lockspace name required\n");
		exit(EXIT_FAILURE);
	}
}

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

void do_join(char *name)
{
	dlm_lshandle_t *dh;
	uint32_t flags = 0;

	printf("Joining lockspace \"%s\", permission %o\n", name, create_mode);
	fflush(stdout);

	if (!opt_dir)
		flags = DLM_LSFL_NODIR;

	dh = dlm_new_lockspace(name, create_mode, flags);
	if (!dh) {
		fprintf(stderr, "dlm_new_lockspace %s error %p %d\n",
			name, dh, errno);
		exit(-1);
	}

	dlm_close_lockspace(dh);
	/* there's no autofree so the ls should stay around */
	printf("done\n");
}

void do_leave(char *name)
{
	dlm_lshandle_t *dh;

	printf("Leaving lockspace \"%s\"\n", name);
	fflush(stdout);

	dh = dlm_open_lockspace(name);
	if (!dh) {
		fprintf(stderr, "dlm_open_lockspace %s error %p %d\n",
			name, dh, errno);
		exit(-1);
	}

	dlm_release_lockspace(name, dh, 1);
	printf("done\n");
}

#define LOCK_LINE_MAX 1024

void do_lockdebug(char *name)
{
	FILE *file;
	char path[PATH_MAX];
	char line[LOCK_LINE_MAX];

	snprintf(path, PATH_MAX, "/sys/kernel/debug/dlm/%s", name);

	file = fopen(path, "r");
	if (!file) {
		fprintf(stderr, "can't open %s: %s\n", path, strerror(errno));
		return;
	}

	while (fgets(line, LOCK_LINE_MAX, file)) {
		printf("%s", line);
	}

	fclose(file);
}

char *mode_str(int mode)
{
	switch (mode) {
	case -1:
		return "IV";
	case LKM_NLMODE:
		return "NL";
	case LKM_CRMODE:
		return "CR";
	case LKM_CWMODE:
		return "CW";
	case LKM_PRMODE:
		return "PR";
	case LKM_PWMODE:
		return "PW";
	case LKM_EXMODE:
		return "EX";
	}
	return "??";
}

/* from linux/fs/dlm/dlm_internal.h */
#define DLM_LKSTS_WAITING       1
#define DLM_LKSTS_GRANTED       2
#define DLM_LKSTS_CONVERT       3

void parse_r_name(char *line, char *name)
{
	char *p;
	int i = 0;
	int begin = 0;

	for (p = line; ; p++) {
		if (*p == '"') {
			if (begin)
				break;
			begin = 1;
			continue;
		}
		if (begin)
			name[i++] = *p;
	}
}

void do_lockdump(char *name)
{
	FILE *file;
	char path[PATH_MAX];
	char line[LOCK_LINE_MAX];
	char r_name[65];
	int r_nodeid;
	int r_len;
	int rv;
	unsigned int time;
	unsigned long long xid;
	uint32_t	id;
	int		nodeid;
	uint32_t	remid;
	int		ownpid;
	uint32_t	exflags;
	uint32_t	flags;
	int8_t		status;
	int8_t		grmode;
	int8_t		rqmode;

	snprintf(path, PATH_MAX, "/sys/kernel/debug/dlm/%s_locks", name);

	file = fopen(path, "r");
	if (!file) {
		fprintf(stderr, "can't open %s: %s\n", path, strerror(errno));
		return;
	}

	/* skip the header on the first line */
	fgets(line, LOCK_LINE_MAX, file);

	while (fgets(line, LOCK_LINE_MAX, file)) {
		rv = sscanf(line, "%x %d %x %u %llu %x %x %hhd %hhd %hhd %u %d %d",
		       &id,
		       &nodeid,
		       &remid,
		       &ownpid,
		       &xid,
		       &exflags,
		       &flags,
		       &status,
		       &grmode,
		       &rqmode,
		       &time,
		       &r_nodeid,
		       &r_len);

		if (rv != 13) {
			fprintf(stderr, "invalid debugfs line %d: %s\n",
				rv, line);
			return;
		}

		memset(r_name, 0, sizeof(r_name));
		parse_r_name(line, r_name);

		/* don't print MSTCPY locks without -M */
		if (!r_nodeid && nodeid) {
			if (!dump_mstcpy)
				continue;
			printf("id %08x gr %s rq %s pid %u MSTCPY %d \"%s\"\n",
				id, mode_str(grmode), mode_str(rqmode),
				ownpid, nodeid, r_name);
			continue;
		}

		/* A hack because dlm-kernel doesn't set rqmode back to IV when
		   a NOQUEUE convert fails, which means in a lockdump it looks
		   like a granted lock is still converting since rqmode is not
		   IV.  (does it make sense to include status in the output,
		   e.g. G,C,W?) */

		if (status == DLM_LKSTS_GRANTED)
			rqmode = LKM_IVMODE;

		printf("id %08x gr %s rq %s pid %u master %d \"%s\"\n",
			id, mode_str(grmode), mode_str(rqmode),
			ownpid, nodeid, r_name);
	}

	fclose(file);
}

void do_spaces(void)
{
	/* TODO: get info from /sys/kernel/config/ */
}

static void do_deadlock_check(char *name)
{
	dlmc_deadlock_check(name);
}

int main(int argc, char **argv)
{
	prog_name = argv[0];
	decode_arguments(argc, argv);
	/* check_name(lsname); */

	switch (operation) {
	case OP_JOIN:
		do_join(lsname);
		break;

	case OP_LEAVE:
		do_leave(lsname);
		break;

	case OP_JOINLEAVE:
		do_join(lsname);
		do_leave(lsname);
		break;

	case OP_LOCKDUMP:
		do_lockdump(lsname);
		break;

	case OP_LOCKDEBUG:
		do_lockdebug(lsname);
		break;

	case OP_SPACES:
		do_spaces();
		break;

	case OP_DEADLOCK_CHECK:
		do_deadlock_check(lsname);
		break;
	}
	return 0;
}

