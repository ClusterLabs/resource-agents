#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <limits.h>
#include <errno.h>

#define __user
#include "gfs_ioctl.h"
#include "osi_list.h"

#include "gfs_tool.h"

#define SIZE (4096)

struct token_list {
	osi_list_t list;
	char *token;
	unsigned int last;
};

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

	for (head = &token_list, tmp = head->next; tmp != head; tmp = tmp->next) {
		tl = osi_list_entry(tmp, struct token_list, list);
		if (strcmp(tl->token, token))
			continue;

		last = tl->last;
		tl->last = this;
		return last;
	}

	tl = malloc(sizeof (struct token_list) + strlen(token) + 1);
	if (!tl)
		die("out of memory\n");
	tl->token = (char *) (tl + 1);
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
 * print_counters - print out the current countersable parameters for a filesystem
 * @argc:
 * @argv:
 *
 */

void
print_counters(int argc, char **argv)
{
	char *fs;
	int fd;

	if (optind < argc)
		fs = argv[optind++];
	else
		die("Usage: gfs_tool counters <mountpoint>\n");

	fd = open(fs, O_RDONLY);
	if (fd < 0)
		die("can't open file %s: %s\n", fs, strerror(errno));

	check_for_gfs(fd, fs);

	for (;;) {
		struct gfs_ioctl gi;
		char *argv[] = { "get_counters" };
		char data[SIZE];
		int error;

		gi.gi_argc = 1;
		gi.gi_argv = argv;
		gi.gi_data = data;
		gi.gi_size = SIZE;

		error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
		if (error < 0)
			die("can't get counters: %s\n", strerror(errno));

		if (debug)
			write(STDOUT_FILENO, data, error);

		parse_lines(data, error);

		if (!continuous)
			break;

		fflush(stdout);

		sleep(interval);
	}

	close(fd);
}
