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
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <limits.h>
#include <errno.h>
#include <libgen.h>

#define __user
#include <linux/gfs2_ioctl.h>
#include <linux/gfs2_ondisk.h>

#include "gfs2_tool.h"

/**
 * check_for_gfs2 - Check to see if a descriptor is a file on a GFS2 filesystem
 * @fd: the file descriptor
 * @path: the path used to open the descriptor
 *
 */

void
check_for_gfs2(int fd, char *path)
{
	unsigned int magic = 0;
	int error;

	error = ioctl(fd, GFS2_IOCTL_IDENTIFY, &magic);
	if (error || magic != GFS2_MAGIC)
		die("%s is not a GFS2 file/filesystem\n",
		    path);
}

/**
 * get_list - Get the list of GFS2 filesystems
 *
 * Returns: a NULL terminated string
 */

#define LIST_SIZE (1048576)

char *
get_list(void)
{
	char *list;
	int fd;
	int x;

	list = malloc(LIST_SIZE);
	if (!list)
		die("out of memory\n");

	fd = open("/proc/fs/gfs2", O_RDWR);
	if (fd < 0)
		die("can't open /proc/fs/gfs2: %s\n",
		    strerror(errno));

	if (write(fd, "list", 4) != 4)
		die("can't write list command: %s\n",
		    strerror(errno));
	x = read(fd, list, LIST_SIZE - 1);
	if (x < 0)
		die("can't get list of filesystems: %s\n",
		    strerror(errno));

	close(fd);

	list[x] = 0;

	return list;
}

/**
 * str2lines - parse a string into lines
 * @list: the list
 *
 * Returns: An array of character pointers
 */

char **
str2lines(char *str)
{
	char *p;
	unsigned int n = 0;
	char **lines;
	unsigned int x = 0;

	for (p = str; *p; p++)
		if (*p == '\n')
			n++;

	lines = malloc((n + 1) * sizeof(char *));
	if (!lines)
		die("out of memory\n");

	for (lines[x] = p = str; *p; p++)
		if (*p == '\n') {
			*p = 0;
			lines[++x] = p + 1;
		}

	return lines;
}

/**
 * do_basename - Create dm-N style name for the device
 * @device:
 *
 * Returns: Pointer to dm name or basename
 */

static char *
do_basename(char *device)
{
	FILE *file;
	int found = FALSE;
	char line[256], major_name[256];
	unsigned int major_number;
	struct stat st;

	file = fopen("/proc/devices", "r");
	if (!file)
		goto punt;

	while (fgets(line, 256, file)) {
		if (sscanf(line, "%u %s", &major_number, major_name) != 2)
			continue;
		if (strcmp(major_name, "device-mapper") != 0)
			continue;
		found = TRUE;
		break;
	}

	fclose(file);

	if (!found)
		goto punt;

	if (stat(device, &st))
		goto punt;
	if (major(st.st_rdev) == major_number) {
		static char realname[16];
		snprintf(realname, 16, "dm-%u", minor(st.st_rdev));
		return realname;
	}

 punt:
	return basename(device);
}

/**
 * mp2cookie - Find the cookie for a filesystem given its mountpoint
 * @mp:
 * @ioctl_ok: If this is FALSE, it's not acceptable to open() the mountpoint
 *
 * Returns: the cookie
 */

char *
mp2cookie(char *mp, int ioctl_ok)
{
	char *cookie;
	char *list, **lines;
	FILE *file;
	char line[256], device[256];
	char *dev = NULL;
	unsigned int x;

	cookie = malloc(256);
	if (!cookie)
		die("out of memory\n");
	list = get_list();
	lines = str2lines(list);

	file = fopen("/proc/mounts", "r");
	if (!file)
		die("can't open /proc/mounts: %s\n",
		    strerror(errno));

	while (fgets(line, 256, file)) {
		char path[256], type[256];

		if (sscanf(line, "%s %s %s", device, path, type) != 3)
			continue;
		if (strcmp(path, mp))
			continue;
		if (strcmp(type, "gfs2"))
			die("%s is not a GFS2 filesystem\n", mp);

		dev = do_basename(device);

		break;
	}

	fclose(file);

	for (x = 0; *lines[x]; x++) {
		char s_id[256];
		sscanf(lines[x], "%s %s", cookie, s_id);
		if (dev) {
			if (strcmp(s_id, dev) == 0)
				return cookie;
		} else {
			if (strcmp(cookie, mp) == 0)
				return cookie;
		}
	}

	if (ioctl_ok) {
		struct gfs2_ioctl gi;
		char *argv[] = { "get_cookie" };
		int fd;

		gi.gi_argc = 1;
		gi.gi_argv = argv;
		gi.gi_data = cookie;
		gi.gi_size = 256;

		fd = open(mp, O_RDONLY);
		if (fd < 0)
			die("can't open %s: %s\n",
			    mp, strerror(errno));

		check_for_gfs2(fd, mp);

		if (ioctl(fd, GFS2_IOCTL_SUPER, &gi) < 0)
			die("can't get cookie for %s: %s\n",
			    mp, strerror(errno));

		close(fd);

		return cookie;
	}

	die("unknown mountpoint %s\n", mp);
}

/**
 * name2value - find the value of a name-value pair in a string
 * @str_in:
 * @name:
 *
 * Returns: the value string in a static buffer
 */

char *
name2value(char *str_in, char *name)
{
	char str[strlen(str_in) + 1];
	static char value[256];
	char **lines;
	unsigned int x;
	unsigned int len = strlen(name);

	strcpy(str, str_in);
	value[0] = 0;

	lines = str2lines(str);

	for (x = 0; *lines[x]; x++)
		if (memcmp(lines[x], name, len) == 0 &&
		    lines[x][len] == ' ') {
			strcpy(value, lines[x] + len + 1);
			break;
		}

	free(lines);

	return value;
}

/**
 * name2u32 - find the value of a name-value pair in a string
 * @str_in:
 * @name:
 *
 * Returns: the value uint32
 */

uint32_t
name2u32(char *str, char *name)
{
	char *value = name2value(str, name);
	uint32_t x = 0;

	sscanf(value, "%u", &x);

	return x;
}

/**
 * name2u64 - find the value of a name-value pair in a string
 * @str_in:
 * @name:
 *
 * Returns: the value uint64
 */

uint64_t
name2u64(char *str, char *name)
{
	char *value = name2value(str, name);
	uint64_t x = 0;

	sscanf(value, "%"SCNu64, &x);

	return x;
}
