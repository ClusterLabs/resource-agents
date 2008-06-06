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

#include "gfs_tool.h"

#define SIZE (4096)

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
	char *gi_argv[] = { "get_tune" };
	struct gfs_ioctl gi;
	char str[SIZE], str2[SIZE];
	char **lines, **l;

	if (optind == argc)
		die("Usage: gfs_tool gettune <mountpoint>\n");

	fd = open(argv[optind], O_RDONLY);
	if (fd < 0)
		die("can't open file %s: %s\n",
		    argv[optind], strerror(errno));

	check_for_gfs(fd, argv[optind]);

	gi.gi_argc = 1;
	gi.gi_argv = gi_argv;
	gi.gi_data = str;
	gi.gi_size = SIZE;

	if (ioctl(fd, GFS_IOCTL_SUPER, &gi) < 0)
		die("error doing get_tune: %s\n",
		    strerror(errno));

	close(fd);

	strcpy(str2, str);
	lines = str2lines(str2);

	for (l = lines; **l; l++) {
		char *p;
		for (p = *l; *p; p++)
			if (*p == ' ') {
				*p++ = 0;
				break;
			}

		if (strcmp(*l, "version") == 0)
			continue;
		if (strcmp(*l, "quota_scale_num") == 0) {
			printf("quota_scale = %.4f   (%u, %u)\n",
			       (double)name2u32(str, "quota_scale_num") / name2u32(str, "quota_scale_den"),
			       name2u32(str, "quota_scale_num"), name2u32(str, "quota_scale_den"));
			continue;
		}
		if (strcmp(*l, "quota_scale_den") == 0)
			continue;

		printf("%s = %s\n", *l, p);
	}
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
	struct gfs_ioctl gi;
	char buf[256];

	if (optind == argc)
		die("Usage: gfs_tool settune <mountpoint> <parameter> <value>\n");
	mp = argv[optind++];
	if (optind == argc)
		die("Usage: gfs_tool settune <mountpoint> <parameter> <value>\n");
	param = argv[optind++];
	if (optind == argc)
		die("Usage: gfs_tool settune <mountpoint> <parameter> <value>\n");
	value = argv[optind++];

	fd = open(mp, O_RDONLY);
	if (fd < 0)
		die("can't open file %s: %s\n",
		    mp, strerror(errno));

	check_for_gfs(fd, mp);

	if (strcmp(param, "quota_scale") == 0) {
		float s;
		sscanf(value, "%f", &s);
		sprintf(buf, "%u %u", (unsigned int)(s * 10000.0 + 0.5), 10000);
		value = buf;
	}

	{
		char *argv[] = { "set_tune", param, value };

		gi.gi_argc = 3;
		gi.gi_argv = argv;

		if (ioctl(fd, GFS_IOCTL_SUPER, &gi))
			die("can't change tunable parameter %s: %s\n",
			    param, strerror(errno));
	}

	close(fd);
}
