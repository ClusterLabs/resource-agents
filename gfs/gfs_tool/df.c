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
#include "gfs_ondisk.h"

#include "gfs_tool.h"

#define SIZE (4096)

/**
 * do_df_one - print out information about one filesystem
 * @path: the path to the filesystem
 *
 */

static void
do_df_one(char *path)
{
	int fd;
	struct gfs_ioctl gi;
	char stat_gfs[SIZE], args[SIZE], lockstruct[SIZE];
	struct gfs_sb sb;
	struct gfs_dinode ji, ri;
	uint64_t journals, rgrps;
	uint64_t used_data;
	unsigned int flags;
	unsigned int percentage;
 	int error;


	fd = open(path, O_RDONLY);
	if (fd < 0)
		die("can't open %s: %s\n", path, strerror(errno));

	check_for_gfs(fd, path);


	{
		char *argv[] = { "get_stat_gfs" };

		gi.gi_argc = 1;
		gi.gi_argv = argv;
		gi.gi_data = stat_gfs;
		gi.gi_size = SIZE;

		error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
		if (error < 0)
			die("error doing get_stat_gfs (%d): %s\n",
			    error, strerror(errno));
	}
	{
		char *argv[] = { "get_super" };

		gi.gi_argc = 1;
		gi.gi_argv = argv;
		gi.gi_data = (char *)&sb;
		gi.gi_size = sizeof(struct gfs_sb);

		error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
		if (error != gi.gi_size)
			die("error doing get_super (%d): %s\n",
			    error, strerror(errno));
	}
	{
		char *argv[] = { "get_args" };

		gi.gi_argc = 1;
		gi.gi_argv = argv;
		gi.gi_data = args;
		gi.gi_size = SIZE;

		error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
		if (error < 0)
			die("error doing get_args (%d): %s\n",
			    error, strerror(errno));
	}
	{
		char *argv[] = { "get_lockstruct" };

		gi.gi_argc = 1;
		gi.gi_argv = argv;
		gi.gi_data = lockstruct;
		gi.gi_size = SIZE;

		error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
		if (error < 0)
			die("error doing get_lockstruct (%d): %s\n",
			    error, strerror(errno));
	}
	{
		char *argv[] = { "get_hfile_stat",
				 "jindex" };

		gi.gi_argc = 2;
		gi.gi_argv = argv;
		gi.gi_data = (char *)&ji;
		gi.gi_size = sizeof(struct gfs_dinode);

		error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
		if (error != gi.gi_size)
			die("error doing get_hfile_stat for jindex (%d): %s\n",
			    error, strerror(errno));
	}
	{
		char *argv[] = { "get_hfile_stat",
				 "rindex" };

		gi.gi_argc = 2;
		gi.gi_argv = argv;
		gi.gi_data = (char *)&ri;
		gi.gi_size = sizeof(struct gfs_dinode);

		error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
		if (error != gi.gi_size)
			die("error doing get_hfile_stat for rindex (%d): %s\n",
			    error, strerror(errno));
	}


	close(fd);


	journals = ji.di_size;
	if (journals % sizeof(struct gfs_jindex))
		die("bad jindex size\n");
	journals /= sizeof(struct gfs_jindex);

	rgrps = ri.di_size;
	if (rgrps % sizeof(struct gfs_rindex))
		die("bad rindex size\n");
	rgrps /= sizeof(struct gfs_rindex);


	used_data = name2u64(stat_gfs, "total_blocks") -
		name2u64(stat_gfs, "free") -
		(name2u64(stat_gfs, "used_dinode") + name2u64(stat_gfs, "free_dinode")) -
		(name2u64(stat_gfs, "used_meta") + name2u64(stat_gfs, "free_meta"));


	printf("%s:\n", path);
	printf("  SB lock proto = \"%s\"\n", sb.sb_lockproto);
	printf("  SB lock table = \"%s\"\n", sb.sb_locktable);
	printf("  SB ondisk format = %u\n", sb.sb_fs_format);
	printf("  SB multihost format = %u\n", sb.sb_multihost_format);
	printf("  Block size = %u\n", name2u32(stat_gfs, "bsize"));
	printf("  Journals = %"PRIu64"\n", journals);
	printf("  Resource Groups = %"PRIu64"\n", rgrps);
	printf("  Mounted lock proto = \"%s\"\n",
	       (name2value(args, "lockproto")[0]) ?
	       name2value(args, "lockproto") : sb.sb_lockproto);
	printf("  Mounted lock table = \"%s\"\n",
	       (name2value(args, "locktable")[0]) ?
	       name2value(args, "locktable") : sb.sb_locktable);
	printf("  Mounted host data = \"%s\"\n", name2value(args, "hostdata"));
	printf("  Journal number = %u\n", name2u32(lockstruct, "jid"));
	flags = name2u32(lockstruct, "flags");
	printf("  Lock module flags = %x", flags);
	printf("\n");
	printf("  Local flocks = %s\n", (name2u32(args, "localflocks")) ? "TRUE" : "FALSE");
	printf("  Local caching = %s\n", (name2u32(args, "localcaching")) ? "TRUE" : "FALSE");
	printf("  Oopses OK = %s\n", (name2u32(args, "oopses_ok")) ? "TRUE" : "FALSE");
	printf("\n");
	printf("  %-15s%-15s%-15s%-15s%-15s\n", "Type", "Total", "Used", "Free", "use%");
	printf("  ------------------------------------------------------------------------\n");

	percentage = (name2u64(stat_gfs, "used_dinode") + name2u64(stat_gfs, "free_dinode")) ?
		(100.0 * name2u64(stat_gfs, "used_dinode") / (name2u64(stat_gfs, "used_dinode") +
							      name2u64(stat_gfs, "free_dinode")) + 0.5) : 0;
	printf("  %-15s%-15"PRIu64"%-15"PRIu64"%-15"PRIu64"%u%%\n",
	       "inodes",
	       name2u64(stat_gfs, "used_dinode") + name2u64(stat_gfs, "free_dinode"),
	       name2u64(stat_gfs, "used_dinode"),
	       name2u64(stat_gfs, "free_dinode"),
	       percentage);

	percentage = (name2u64(stat_gfs, "used_meta") + name2u64(stat_gfs, "free_meta")) ?
		(100.0 * name2u64(stat_gfs, "used_meta") / (name2u64(stat_gfs, "used_meta") +
							    name2u64(stat_gfs, "free_meta")) + 0.5) : 0;
	printf("  %-15s%-15"PRIu64"%-15"PRIu64"%-15"PRIu64"%u%%\n",
	       "metadata",
	       name2u64(stat_gfs, "used_meta") + name2u64(stat_gfs, "free_meta"),
	       name2u64(stat_gfs, "used_meta"),
	       name2u64(stat_gfs, "free_meta"),
	       percentage);

	percentage = (used_data + name2u64(stat_gfs, "free")) ?
		(100.0 * used_data / (used_data + name2u64(stat_gfs, "free")) + 0.5) : 0;
	printf("  %-15s%-15"PRIu64"%-15"PRIu64"%-15"PRIu64"%u%%\n",
	       "data",
	       used_data + name2u64(stat_gfs, "free"),
	       used_data,
	       name2u64(stat_gfs, "free"),
	       percentage);
}


/**
 * print_df - print out information about filesystems
 * @argc:
 * @argv:
 *
 */

void
print_df(int argc, char **argv)
{
	if (optind < argc) {
		char buf[PATH_MAX];

		if (!realpath(argv[optind], buf))
			die("can't determine real path: %s\n", strerror(errno));

		do_df_one(buf);

		return;
	}

	{
		FILE *file;
		char buf[256], device[256], path[256], type[256];
		int first = TRUE;

		file = fopen("/proc/mounts", "r");
		if (!file)
			die("can't open /proc/mounts: %s\n", strerror(errno));

		while (fgets(buf, 256, file)) {
			if (sscanf(buf, "%s %s %s", device, path, type) != 3)
				continue;
			if (strcmp(type, "gfs") != 0)
				continue;

			if (first)
				first = FALSE;
			else
				printf("\n");

			do_df_one(path);
		}

		fclose(file);
	}
}


