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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <linux/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>

#define __user
#include "osi_list.h"

#include "gfs2_tool.h"

#define SIZE (65536)

struct token_list {
	osi_list_t list;
	char *token;
	unsigned int last;
};

static osi_list_decl(token_list);
int first = TRUE;

#define maybe_printf(fmt, args...) \
do { \
	if (!continuous || !first) \
		printf(fmt, ##args); \
} while (0)

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
 * @diff: 1 if you want diffs on continuous output
 *
 */

static void
print_line(char *fsname, char *token, char *description, int diff)
{
	char *value;
	char counters_base[PATH_MAX] = "counters/";
	static unsigned int log_blks_free;
	unsigned int this, last;

	value = get_sysfs(fsname, strcat(counters_base, token));
	
	if (!strcmp(token, "log_blks_free"))
		sscanf(value, "%u", &log_blks_free);

	else if (!strcmp(token, "jd_blocks")) {
		sscanf(value, "%u", &this);
		maybe_printf("%39s %.2f%% (%u of %u)\n",
			     "log space used",
			     100.0 * (this - log_blks_free) / this,
			     this - log_blks_free, this);

	} else if (continuous && diff) {
		sscanf(value, "%u", &this);
		last = find_update_last(token, this);
		maybe_printf("%39s %-10s %d/s\n",
			     description, value,
			     (this - last + interval - 1) / interval);

	} else
		maybe_printf("%39s %s\n", description, value);
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
	unsigned int i = interval;
	char *path, *fs;
	int fd;

	interval = 1;

	if (optind < argc)
		path = argv[optind++];
	else
		die("Usage: gfs2_tool counters <mountpoint>\n");

	fd = open(path, O_RDONLY);
	if (fd < 0)
		die("can't open file %s: %s\n", path, strerror(errno));

	check_for_gfs2(fd, path);
	close(fd);

	fs = mp2fsname(path);

	for (;;) {
		print_line(fs, "glock_count", "locks", 0);
		print_line(fs, "glock_held_count", "locks held", 0);
		print_line(fs, "inode_count", "incore inodes", 0);
		print_line(fs, "reclaimed", "glocks reclaimed", 1);
#if GFS2_TOOL_FEATURE_IMPLEMENTED
		print_line(fs, "bufdata_count", "metadata buffers", 0);
		print_line(fs, "unlinked_count", "unlinked inodes", 0);
		print_line(fs, "quota_count", "quota IDs", 0);
		print_line(fs, "log_num_gl", "Glocks in current transaction",
			   0);
		print_line(fs, "log_num_buf", "Blocks in current transaction",
			   0);
		print_line(fs, "log_num_revoke",
			   "Revokes in current transaction", 0);
		print_line(fs, "log_num_rg", "RGs in current transaction", 0);
		print_line(fs, "log_num_databuf",
			   "Databufs in current transaction", 0);
		print_line(fs, "log_blks_free", "log blks free", 0);
		print_line(fs, "jd_blocks", "log blocks total", 0);
		print_line(fs, "reclaim_count", "glocks on reclaim list", 0);
		print_line(fs, "log_wraps", "log wraps", 0);
		print_line(fs, "fh2dentry_misses", "fh2dentry misses", 1);
		print_line(fs, "log_flush_incore", "log incore flushes", 1);
		print_line(fs, "log_flush_ondisk", "log ondisk flushes", 1);
		print_line(fs, "glock_nq_calls", "glock dq calls", 1);
		print_line(fs, "glock_dq_calls", "glock dq calls", 1);
		print_line(fs, "glock_prefetch_calls", "glock prefetch calls",
			   1);
		print_line(fs, "lm_lock_calls", "lm_lock calls", 1);
		print_line(fs, "lm_unlock_calls", "lm_unlock calls", 1);
		print_line(fs, "lm_callbacks", "lm callbacks", 1);
		print_line(fs, "ops_address", "address operations", 1);
		print_line(fs, "ops_dentry", "dentry operations", 1);
		print_line(fs, "ops_export", "export operations", 1);
		print_line(fs, "ops_file", "file operations", 1);
		print_line(fs, "ops_inode", "inode operations", 1);
		print_line(fs, "ops_super", "super operations", 1);
		print_line(fs, "ops_vm", "vm operations", 1);
#endif /* #if GFS2_TOOL_FEATURE_IMPLEMENTED */

		if (!continuous)
			break;

		fflush(stdout);

		sleep(interval);

		if (first) {
			interval = i;
			first = FALSE;
		}
	}

	close(fd);
}
