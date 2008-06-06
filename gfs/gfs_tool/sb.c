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

#include "gfs_ondisk.h"

#include "gfs_tool.h"

#define do_lseek(fd, off) \
do { \
	if (lseek((fd), (off), SEEK_SET) != (off)) \
		die("bad seek: %s on line %d of file %s\n", \
		    strerror(errno),__LINE__, __FILE__); \
} while (0)

#define do_read(fd, buff, len) \
do { \
	if (read((fd), (buff), (len)) != (len)) \
		die("bad read: %s on line %d of file %s\n", \
		    strerror(errno), __LINE__, __FILE__); \
} while (0)

#define do_write(fd, buff, len) \
do { \
	if (write((fd), (buff), (len)) != (len)) \
		die("bad write: %s on line %d of file %s\n", \
		    strerror(errno), __LINE__, __FILE__); \
} while (0)

/**
 * do_sb - examine/modify a unmounted FS' superblock
 * @argc:
 * @argv:
 *
 */

void
do_sb(int argc, char **argv)
{
	char *device, *field, *newval = NULL;
	int fd;
	unsigned char buf[GFS_BASIC_BLOCK], input[256];
	struct gfs_sb sb;

	if (optind == argc)
		die("Usage: gfs_tool sb <device> <field> [newval]\n");

	device = argv[optind++];

	if (optind == argc)
		die("Usage: gfs_tool sb <device> <field> [newval]\n");

	field = argv[optind++];

	if (optind < argc) {
		if (strcmp(field, "all") == 0)
			die("can't specify new value for \"all\"\n");
		newval = argv[optind++];
	}


	fd = open(device, (newval) ? O_RDWR : O_RDONLY);
	if (fd < 0)
		die("can't open %s: %s\n", device, strerror(errno));

	if (newval && !override) {
		printf("You shouldn't change any of these values if the filesystem is mounted.\n");
		printf("\nAre you sure? [y/n] ");
		fgets(input, 255, stdin);

		if (input[0] != 'y')
			die("aborted\n");

		printf("\n");
	}

	do_lseek(fd, GFS_SB_ADDR * GFS_BASIC_BLOCK);
	do_read(fd, buf, GFS_BASIC_BLOCK);

	gfs_sb_in(&sb, buf);

	if (sb.sb_header.mh_magic != GFS_MAGIC ||
	    sb.sb_header.mh_type != GFS_METATYPE_SB)
		die("there isn't a GFS filesystem on %s\n", device);

	if (strcmp(field, "proto") == 0) {
		printf("current lock protocol name = \"%s\"\n",
		       sb.sb_lockproto);

		if (newval) {
			if (strlen(newval) >= GFS_LOCKNAME_LEN)
				die("new lockproto name is too long\n");
			strcpy(sb.sb_lockproto, newval);
			printf("new lock protocol name = \"%s\"\n",
			       sb.sb_lockproto);
		}
	} else if (strcmp(field, "table") == 0) {
		printf("current lock table name = \"%s\"\n",
		       sb.sb_locktable);

		if (newval) {
			if (strlen(newval) >= GFS_LOCKNAME_LEN)
				die("new locktable name is too long\n");
			strcpy(sb.sb_locktable, newval);
			printf("new lock table name = \"%s\"\n",
			       sb.sb_locktable);
		}
	} else if (strcmp(field, "ondisk") == 0) {
		printf("current ondisk format = %u\n",
		       sb.sb_fs_format);

		if (newval) {
			sb.sb_fs_format = atoi(newval);
			printf("new ondisk format = %u\n",
			       sb.sb_fs_format);
		}
	} else if (strcmp(field, "multihost") == 0) {
		printf("current multihost format = %u\n",
		       sb.sb_multihost_format);

		if (newval) {
			sb.sb_multihost_format = atoi(newval);
			printf("new multihost format = %u\n",
			       sb.sb_multihost_format);
		}
	} else if (strcmp(field, "all") == 0) {
		gfs_sb_print(&sb);
		newval = FALSE;
	} else
		die("unknown field %s\n", field);

	if (newval) {
		gfs_sb_out(&sb, buf);

		do_lseek(fd, GFS_SB_ADDR * GFS_BASIC_BLOCK);
		do_write(fd, buf, GFS_BASIC_BLOCK);

		fsync(fd);

		printf("Done\n");
	}

	close(fd);
}
