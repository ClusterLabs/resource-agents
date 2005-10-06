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
#include <dirent.h>

#define __user
#include <linux/gfs2_ioctl.h>

#include "gfs2_tool.h"

#define SIZE (65536)

/**
 * get_tune - print out the current tuneable parameters for a filesystem
 * @argc:
 * @argv:
 *
 */

void
get_tune(int argc, char **argv)
{
	int fd;
	char path[PATH_MAX];
	char *fs;
	DIR *d;
	struct dirent *de;
	double ratio;
	unsigned int num, den;

	if (optind == argc)
		die("Usage: gfs2_tool gettune <mountpoint>\n");

	fd = open(argv[optind], O_RDONLY);
	if (fd < 0)
		die("can't open file %s: %s\n",
		    argv[optind], strerror(errno));

	check_for_gfs2(fd, argv[optind]);
	close(fd);
	fs = mp2fsname(argv[optind]);
	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX - 1, "%s/%s/tune", SYS_BASE, fs);

	d = opendir(path);
	if (!d)
		die("can't open %s: %s\n", path, strerror(errno));

	while((de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;
		snprintf(path, PATH_MAX - 1, "tune/%s", de->d_name);
		if (strcmp(de->d_name, "quota_scale") == 0) {
			sscanf(get_sysfs(fs, "tune/quota_scale"), "%u %u",
			       &num, &den);
			ratio = (double)num / den;
			printf("quota_scale = %.4f   (%u, %u)\n", ratio, num,
			       den);
		} else
			printf("%s = %s\n", de->d_name, get_sysfs(fs, path));
	}
	closedir(d);
}

/**
 * set_tune - set a tuneable parameter
 * @argc:
 * @argv:
 *
 */

void
set_tune(int argc, char **argv)
{
	char *mp, *param, *value;
	int fd;
	char tune_base[SIZE] = "tune/";
	char buf[256];
	char *fs;

	if (optind == argc)
		die("Usage: gfs2_tool settune <mountpoint> <parameter> <value>\n");
	mp = argv[optind++];
	if (optind == argc)
		die("Usage: gfs2_tool settune <mountpoint> <parameter> <value>\n");
	param = argv[optind++];
	if (optind == argc)
		die("Usage: gfs2_tool settune <mountpoint> <parameter> <value>\n");
	value = argv[optind++];

	fd = open(mp, O_RDONLY);
	if (fd < 0)
		die("can't open file %s: %s\n",
		    mp, strerror(errno));

	check_for_gfs2(fd, mp);
	close(fd);
	fs = mp2fsname(mp);

	if (strcmp(param, "quota_scale") == 0) {
		float s;
		sscanf(value, "%f", &s);
		sprintf(buf, "%u %u", (unsigned int)(s * 10000.0 + 0.5), 10000);
		value = buf;
	}
	set_sysfs(fs, strcat(tune_base, param), value);
}
