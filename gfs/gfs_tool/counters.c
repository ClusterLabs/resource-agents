 /******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>

#include "global.h"
#include <linux/gfs_ioctl.h>
#include "osi_list.h"

#include "gfs_tool.h"



#define SIZE (65536)



struct token_list
{
	osi_list_t list;
	char *token;
	unsigned int last;
};



static int continuous = FALSE;
static unsigned int interval = 1;
static int debug = FALSE;
static char *filesystem = NULL;

static osi_list_decl(token_list);





/**
 * find_update_last - find and update the last value of a token
 * @token: the token to look for
 * @this: the current value of the token
 *
 * Returns: the last value of the token
 */

static unsigned int
find_update_last(char *token, unsigned int this)
{
	osi_list_t *tmp, *head;
	struct token_list *tl;
	unsigned int last;

	for (head = &token_list, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next)
	{
		tl = osi_list_entry(tmp, struct token_list, list);
		if (strcmp(tl->token, token))
			continue;

		last = tl->last;
		tl->last = this;
		return last;
	}

	tl = malloc(sizeof(struct token_list) + strlen(token) + 1);
	if (!tl)
		die("out of memory\n");
	tl->token = (char *)(tl + 1);
	strcpy(tl->token, token);
	tl->last = this;
	osi_list_add(&tl->list, &token_list);

	return 0;
}

/**
 * print_line - print out a counter
 * @token: the name of the counter
 * @description: the text description of the counter
 * @option: an optional modifier
 * @value: the value of the counter
 *
 */

static void
print_line(char *token, char *description, char *option, char *value)
{
	static unsigned int sd_log_seg_free;
	unsigned int this, last;

	if (!strcmp(token, "sd_log_seg_free"))
		sscanf(value, "%u", &sd_log_seg_free);

	else if (!strcmp(token, "ji_nsegment")) {
		sscanf(value, "%u", &this);
		printf("%39s %.2f%%\n",
		       "log space used",
		       100.0 * (this - sd_log_seg_free) / this);

	} else if (continuous && !strcmp(option, "diff")) {
		sscanf(value, "%u", &this);
		last = find_update_last(token, this);
		printf("%39s %-10s %d/s\n",
		       description, value,
		       (this - last + interval - 1) / interval);

	} else
		printf("%39s %s\n", description, value);
}

/**
 * parse_line: break up a chunk of data into counter fields
 * @buf: the data
 * @count: the number of bytes of data
 *
 */

static void
parse_lines(char *buf, unsigned int count)
{
	char line[SIZE];
	char part1[SIZE], part2[SIZE], part3[SIZE], part4[SIZE];
	char *c, *c2;
	unsigned int x;

	printf("\n");

	while (count) {
		for (c = line; count; c++) {
			*c = *buf;
			buf++;
			count--;
			if (*c == '\n')
				break;
		}
		*c = 0;

		*part1 = *part2 = *part3 = *part4 = 0;

		for (c = line, x = 0; (c2 = strsep(&c, ":")); x++) {
			if (!*c2)
				continue;

			if (x == 0)
				strcpy(part1, c2);
			else if (x == 1)
				strcpy(part2, c2);
			else if (x == 2)
				strcpy(part3, c2);
			else
				strcpy(part4, c2);
		}

		if (x == 4)
			print_line(part1, part2, part3, part4);
	}
}

/**
 * do_args - parse command line arguments
 * @argc:
 * @argv:
 *
 */

static void
do_args(int argc, char **argv)
{
	int cont = TRUE;
	int optchar;

	while (cont) {
		optchar = getopt(argc, argv, "ci:D");

		switch (optchar) {
		case 'c':
			continuous = TRUE;
			break;

		case 'i':
			sscanf(optarg, "%u", &interval);
			if (!interval)
				die("interval must be greater than zero\n");
			break;

		case 'D':
			debug = TRUE;
			break;

		case 'h':
			printf("Usage: gfs_tool [-c,-i] counters <GFS_directory>\n");
			exit(EXIT_SUCCESS);

		case '?':
			exit(EXIT_FAILURE);      

		case EOF:
			cont = FALSE;
			break;

		default:
			die("unknown option: %c\n", optchar);
		}
	}


	for (; optind < argc; optind++) {
		if (!strcmp(argv[optind], "counters"))
			continue;
		else if (!filesystem)
			filesystem = argv[optind];
		else
			die("unrecognized option: %s\n", argv[optind]);
	}

	if (!filesystem)
		die("no filesystem specified\n");
}

/**
 * get_counters - print out the current countersable parameters for a filesystem
 * @argc:
 * @argv:
 *
 */

void
get_counters(int argc, char **argv)
{
	char buf[SIZE];
	struct gfs_user_buffer ub = { .ub_data = buf, .ub_size = SIZE };
	int fd;
	int error;

	do_args(argc, argv);

	fd = open(filesystem, O_RDONLY);
	if (fd < 0)
		die("can't open file %s: %s\n", filesystem, strerror(errno));

	check_for_gfs(fd, filesystem);

	for (;;) {
		error = ioctl(fd, GFS_GET_COUNTERS, &ub);
		if (error)
			die("can't get counters: %s\n", strerror(errno));

		if (debug)
			write(STDOUT_FILENO, buf, ub.ub_count);

		parse_lines(buf, ub.ub_count);

		if (!continuous)
			break;

		fflush(stdout);

		sleep(interval);
	}

	close(fd);
}


