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
#include <netinet/in.h>

#include "libgroup.h"
#include "groupd.h"

#define MAX_GROUPS			64
#define OPTION_STRING			"hV"
#define DUMP_SIZE			(1024 * 1024)

#define OP_LS				1
#define OP_DUMP				2

static char *prog_name;
static int operation;
static int opt_ind;
static char *ls_name;

static void print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s [options]\n", prog_name);
	printf("\n");
	printf("Options:\n");
	printf("\n");
	printf("  -h               Print this help, then exit\n");
	printf("  -V               Print program version information, then exit\n");
}

static void decode_arguments(int argc, char **argv)
{
	int cont = 1;
	int optchar;

	while (cont) {
		optchar = getopt(argc, argv, OPTION_STRING);

		switch (optchar) {

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
		}
		optind++;
	}

	if (!operation)
		operation = OP_LS;
}

char *state_str(group_data_t *data)
{
	static char buf[32];
	
	memset(buf, 0, sizeof(buf));

	sprintf(buf, "E%d U%d R%d F%x",
		data->event_state, data->update_state, data->recover_state,
		data->flags);

	return buf;
}

int do_ls(int argc, char **argv)
{
	group_data_t data[MAX_GROUPS];
	int i, j, rv, count = 0, level;
	char *name;
	int program_width = 16;
	int level_width = 5;
	int name_width = 32;
	int id_width = 8;
	int state_width = 12;

	memset(&data, 0, sizeof(data));

	if (opt_ind && opt_ind < argc) {
		level = atoi(argv[opt_ind++]);
		name = argv[opt_ind];

		rv = group_get_group(level, name, data);
		count = 1;
	} else
		rv = group_get_groups(MAX_GROUPS, &count, data);

	printf("%-*s %-*s %-*s %-*s %-*s\n",
		program_width, "program",
		level_width, "level",
		name_width, "name",
		id_width, "id",
		state_width, "state");

	for (i = 0; i < count; i++) {

		printf("%-*s %-*d %-*s %0*x %-*s\n",
			program_width, data[i].client_name,
			level_width, data[i].level,
			name_width, data[i].name,
			id_width, data[i].id,
			state_width, state_str(&data[i]));

		printf("[");
		for (j = 0; j < data[i].member_count; j++) {
			if (j != 0)
				printf(" ");
			printf("%d", data[i].members[j]);
		}
		printf("]\n");
	}

}

static int connect_groupd(void)
{
	struct sockaddr_un sun;
	socklen_t addrlen;
	int i, rv, fd;

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

int do_dump(int argc, char **argv)
{
	char buf[DUMP_SIZE];
	int i, rv, fd = connect_groupd();

	rv = write(fd, "dump", 4);
	if (rv != 4)
		return -1;

	memset(buf, 0, sizeof(buf));

	rv = read(fd, buf, sizeof(buf));
	if (rv <= 0)
		return rv;

	write(STDOUT_FILENO, buf, rv);

	close(fd);
	return 0;
}

int main(int argc, char **argv)
{
	prog_name = argv[0];
	decode_arguments(argc, argv);

	switch (operation) {
	case OP_LS:
		return do_ls(argc, argv);
	case OP_DUMP:
		return do_dump(argc, argv);
	}

	return 0;
}

