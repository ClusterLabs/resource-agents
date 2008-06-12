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

/**
 * do_file_flush - 
 * @argc:
 * @argv:
 *
 */

void
do_file_flush(int argc, char **argv)
{
	char *gi_argv[] = { "do_file_flush" };
	struct gfs_ioctl gi;
	int fd;
	int error;

	if (optind == argc)
		die("Usage: gfs_tool flush <filenames>\n");

	gi.gi_argc = 1;
	gi.gi_argv = gi_argv;

	for (; optind < argc; optind++) {
		fd = open(argv[optind], O_RDONLY);
		if (fd < 0)
			die("can't open %s: %s\n", argv[optind], strerror(errno));

		check_for_gfs(fd, argv[optind]);

		error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
		if (error)
			die("error doing do_file_flush (%d): %s\n",
			    error, strerror(errno));

		close(fd);
	}
}

/**
 * do_freeze - freeze a GFS filesystem
 * @argc:
 * @argv:
 *
 */

void
do_freeze(int argc, char **argv)
{
	char *command = argv[optind - 1];
	char *cookie;
	int fd;
	char buf[256];
	int x;

	if (optind == argc)
		die("Usage: gfs_tool %s <mountpoint>\n",
		    command);

	cookie = mp2cookie(argv[optind], FALSE);
	x = sprintf(buf, "%s %s\n", command, cookie);

	fd = open("/proc/fs/gfs", O_RDWR);
	if (fd < 0)
		die("can't open /proc/fs/gfs: %s\n",
		    strerror(errno));

	if (write(fd, buf, x) != x)
		die("can't write %s command: %s\n",
		    command, strerror(errno));
	if (read(fd, buf, 256))
		die("can't %s %s: %s\n",
		    command, argv[optind], strerror(errno));

	close(fd);
	sync();
}

/**
 * print_lockdump -
 * @argc:
 * @argv:
 *
 */

void
print_lockdump(int argc, char **argv)
{
	int fd;
	char *mp, *cookie;
	unsigned int size = 4194304;
	char *data;
	char command[256];
	int retry = TRUE;
	int x, count;


	if (optind < argc)
		mp = argv[optind++];
	else
		die("Usage: gfs_tool lockdump <mountpoint> [buffersize]\n");

	if (optind < argc) {
		sscanf(argv[optind++], "%u", &size);
		retry = FALSE;
	}

	cookie = mp2cookie(mp, FALSE);
	x = sprintf(command, "lockdump %s\n", cookie);


	fd = open("/proc/fs/gfs", O_RDWR);
	if (fd < 0)
		die("can't open /proc/fs/gfs: %s\n",
		    strerror(errno));

	for (;;) {
		data = malloc(size);
		if (!data)
			die("out of memory\n");

		if (write(fd, command, x) != x)
			die("can't write lockdump command: %s\n",
			    strerror(errno));
		count = read(fd, data, size);
		if (count >= 0)
			break;

		if (errno == ENOMEM) {
			if (retry) {
				free(data);
				size += 4194304;
				continue;
			} else
				die("%u bytes isn't enough memory\n", size);
		}
		die("error doing lockdump: %s\n",
		    strerror(errno));
	}

	close(fd);


	x = write(STDOUT_FILENO, data, count);

	free(data);
}

/**
 * margs -
 * @argc:
 * @argv:
 *
 */

void
margs(int argc, char **argv)
{
	int fd;
	char *buf;
	unsigned int x;

	if (optind == argc)
		die("Usage: gfs_tool margs <mountarguments>\n");

	x = strlen(argv[optind]) + 7;
	buf = malloc(x + 1);
	if (!buf)
		die("out of memory\n");
	sprintf(buf, "margs %s\n", argv[optind]);

	fd = open("/proc/fs/gfs", O_RDWR);
	if (fd < 0)
		die("can't open /proc/fs/gfs: %s\n",
		    strerror(errno));
 
	if (write(fd, buf, x) != x)
		die("can't write margs command: %s\n",
		    strerror(errno));
	if (read(fd, buf, x))
		die("can't set mount args: %s\n",
		    strerror(errno));

	close(fd);
}

/**
 * print_flags - print the flags in a dinode's di_flags field
 * @di: the dinode structure
 *
 */

static void
print_flags(struct gfs_dinode *di)
{
	if (di->di_flags) {
		printf("Flags:\n");
		if (di->di_flags & GFS_DIF_JDATA)
			printf("  jdata\n");
		if (di->di_flags & GFS_DIF_EXHASH)
			printf("  exhash\n");
		if (di->di_flags & GFS_DIF_UNUSED)
			printf("  unused\n");
		if (di->di_flags & GFS_DIF_EA_INDIRECT)
			printf("  ea_indirect\n");
		if (di->di_flags & GFS_DIF_DIRECTIO)
			printf("  directio\n");
		if (di->di_flags & GFS_DIF_IMMUTABLE)
			printf("  immutable\n");
		if (di->di_flags & GFS_DIF_APPENDONLY)
			printf("  appendonly\n");
#if 0
		if (di->di_flags & GFS_DIF_NOATIME)
			printf("  noatime\n");
		if (di->di_flags & GFS_DIF_SYNC)
			printf("  sync\n");
#endif
		if (di->di_flags & GFS_DIF_INHERIT_DIRECTIO)
			printf("  inherit_directio\n");
		if (di->di_flags & GFS_DIF_INHERIT_JDATA)
			printf("  inherit_jdata\n");
	}
}

/**
 * set_flag - set or clear flags in some dinodes
 * @argc:
 * @argv:
 *
 */

void
set_flag(int argc, char **argv)
{
	struct gfs_dinode di;
	char *set;
	char *flag;
	struct gfs_ioctl gi;
	int fd;
	int error;

	if (optind == argc) {
		di.di_flags = 0xFFFFFFFF;
		print_flags(&di);
		return;
	}

	set = (strcmp(argv[optind - 1], "setflag") == 0) ? "set" : "clear";
	flag = argv[optind++];

	for (; optind < argc; optind++) {
		fd = open(argv[optind], O_RDONLY);
		if (fd < 0)
			die("can't open %s: %s\n", argv[optind], strerror(errno));

		check_for_gfs(fd, argv[optind]);

		{
			char *gi_argv[] = { "set_file_flag",
					    set,
					    flag };
			gi.gi_argc = 3;
			gi.gi_argv = gi_argv;

			error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
			if (error)
				die("can't change flag on %s: %s\n", argv[optind], strerror(errno));
		}

		close(fd);
	}
}

/**
 * print_stat - print out the struct gfs_dinode for a file
 * @argc:
 * @argv:
 *
 */

void
print_stat(int argc, char **argv)
{
	int fd;
	char *gi_argv[] = { "get_file_stat" };
	struct gfs_ioctl gi;
	struct gfs_dinode di;
	int error;

	if (optind == argc)
		die("Usage: gfs_tool stat <filename>\n");

	fd = open(argv[optind], O_RDONLY);
	if (fd < 0)
		die("can't open %s: %s\n", argv[optind], strerror(errno));

	check_for_gfs(fd, argv[optind]);

	gi.gi_argc = 1;
	gi.gi_argv = gi_argv;
	gi.gi_data = (char *)&di;
	gi.gi_size = sizeof(struct gfs_dinode);

	error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
	if (error != gi.gi_size)
		die("error doing get_file_stat (%d): %s\n",
		    error, strerror(errno));

	close(fd);

	gfs_dinode_print(&di);
	printf("\n");
	print_flags(&di);
}

/**
 * print_sb - the superblock
 * @argc:
 * @argv:
 *
 */

void
print_sb(int argc, char **argv)
{
	int fd;
	char *gi_argv[] = { "get_super" };
	struct gfs_ioctl gi;
	struct gfs_sb sb;
	int error;

	if (optind == argc)
		die("Usage: gfs_tool getsb <mountpoint>\n");

	fd = open(argv[optind], O_RDONLY);
	if (fd < 0)
		die("can't open %s: %s\n", argv[optind], strerror(errno));
	
	check_for_gfs(fd, argv[optind]);

	gi.gi_argc = 1;
	gi.gi_argv = gi_argv;
	gi.gi_data = (char *)&sb;
	gi.gi_size = sizeof(struct gfs_sb);

	error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
	if (error != gi.gi_size)
		die("error doing get_super (%d): %s\n",
		    error, strerror(errno));

	close(fd);

	gfs_sb_print(&sb);
}

/**
 * print_jindex - print out the journal index
 * @argc:
 * @argv:
 *
 */

void
print_jindex(int argc, char **argv)
{
	int fd;
	struct gfs_ioctl gi;
	uint64_t offset;
	unsigned int x;
	int error;


	if (optind == argc)
		die("Usage: gfs_tool jindex <mountpoint>\n");

	fd = open(argv[optind], O_RDONLY);
	if (fd < 0)
		die("can't open %s: %s\n", argv[optind], strerror(errno));

	check_for_gfs(fd, argv[optind]);


	{
		char *argv[] = { "get_hfile_stat",
				 "jindex" };
		struct gfs_dinode di;

		gi.gi_argc = 2;
		gi.gi_argv = argv;
		gi.gi_data = (char *)&di;
		gi.gi_size = sizeof(struct gfs_dinode);

		error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
		if (error != gi.gi_size)
			die("error doing get_hfile_stat (%d): %s\n",
			    error, strerror(errno));

		gfs_dinode_print(&di);
	}


	for (offset = 0, x = 0; ; offset += sizeof(struct gfs_jindex), x++) {
		char *argv[] = { "do_hfile_read",
				 "jindex" };
		char buf[sizeof(struct gfs_jindex)];
		struct gfs_jindex ji;
		
		gi.gi_argc = 2;
		gi.gi_argv = argv;
		gi.gi_data = buf;
		gi.gi_size = sizeof(struct gfs_jindex);
		gi.gi_offset = offset;

		error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
		if (!error)
			break;
		if (error != sizeof(struct gfs_jindex))
			die("error doing do_hfile_read (%d): %s\n",
			    error, strerror(errno));

		gfs_jindex_in(&ji, buf);

		printf("\nJournal %u:\n\n", x);
		gfs_jindex_print(&ji);
	}


	close(fd);
}

/**
 * print_rindex - print out the journal index
 * @argc:
 * @argv:
 *
 */

void
print_rindex(int argc, char **argv)
{
	int fd;
	struct gfs_ioctl gi;
	uint64_t offset;
	unsigned int x;
	int error;


	if (optind == argc)
		die("Usage: gfs_tool rindex <mountpoint>\n");

	fd = open(argv[optind], O_RDONLY);
	if (fd < 0)
		die("can't open %s: %s\n", argv[optind], strerror(errno));

	check_for_gfs(fd, argv[optind]);


	{
		char *argv[] = { "get_hfile_stat",
				 "rindex" };
		struct gfs_dinode di;

		gi.gi_argc = 2;
		gi.gi_argv = argv;
		gi.gi_data = (char *)&di;
		gi.gi_size = sizeof(struct gfs_dinode);

		error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
		if (error != gi.gi_size)
			die("error doing get_hfile_stat (%d): %s\n",
			    error, strerror(errno));

		gfs_dinode_print(&di);
	}


	for (offset = 0, x = 0; ; offset += sizeof(struct gfs_rindex), x++) {
		char *argv[] = { "do_hfile_read",
				 "rindex" };
		char buf[sizeof(struct gfs_rindex)];
		struct gfs_rindex ri;
		
		gi.gi_argc = 2;
		gi.gi_argv = argv;
		gi.gi_data = buf;
		gi.gi_size = sizeof(struct gfs_rindex);
		gi.gi_offset = offset;

		error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
		if (!error)
			break;
		if (error != sizeof(struct gfs_rindex))
			die("error doing do_hfile_read (%d): %s\n",
			    error, strerror(errno));

		gfs_rindex_in(&ri, buf);

		printf("\nRG %u:\n\n", x);
		gfs_rindex_print(&ri);
	}


	close(fd);
}

/**
 * print_quota - print out the journal index
 * @argc:
 * @argv:
 *
 */

void
print_quota(int argc, char **argv)
{
	int fd;
	struct gfs_ioctl gi;
	uint64_t offset;
	unsigned int x;
	int error;


	if (optind == argc)
		die("Usage: gfs_tool quota <mountpoint>\n");

	fd = open(argv[optind], O_RDONLY);
	if (fd < 0)
		die("can't open %s: %s\n", argv[optind], strerror(errno));

	check_for_gfs(fd, argv[optind]);


	{
		char *argv[] = { "get_hfile_stat",
				 "quota" };
		struct gfs_dinode di;

		gi.gi_argc = 2;
		gi.gi_argv = argv;
		gi.gi_data = (char *)&di;
		gi.gi_size = sizeof(struct gfs_dinode);

		error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
		if (error != gi.gi_size)
			die("error doing get_hfile_stat (%d): %s\n",
			    error, strerror(errno));

		gfs_dinode_print(&di);
	}


	for (offset = 0, x = 0; ; offset += sizeof(struct gfs_quota), x++) {
		char *argv[] = { "do_hfile_read",
				 "quota" };
		char buf[sizeof(struct gfs_quota)];
		struct gfs_quota q;
		
		gi.gi_argc = 2;
		gi.gi_argv = argv;
		gi.gi_data = buf;
		gi.gi_size = sizeof(struct gfs_quota);
		gi.gi_offset = offset;

		error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
		if (!error)
			break;
		if (error != sizeof(struct gfs_quota))
			die("error doing do_hfile_read (%d): %s\n",
			    error, strerror(errno));

		gfs_quota_in(&q, buf);

		if (q.qu_limit || q.qu_warn || q.qu_value) {
			printf("\nQuota %s %u:\n\n", (x & 1) ? "group" : "user", x >> 1);
			gfs_quota_print(&q);
		}
	}


	close(fd);
}

/**
 * print_list - print the list of mounted filesystems
 *
 */

void
print_list(void)
{
	char *list = get_list();
	printf("%s", list);
}

/**
 * reclaim_metadata - reclaim unused metadata blocks
 * @argc:
 * @argv:
 *
 * This routine uses an ioctl command to quiesce the cluster and then
 * hunt down and free all disk inodes that have been freed.  This will
 * gain back meta data blocks to be used for data (or metadata) again.
 *
 */

void
reclaim_metadata(int argc, char **argv)
{
	int fd;
	char *gi_argv[] = { "do_reclaim" };
	struct gfs_ioctl gi;
	char buf[256];

	if (optind == argc)
		die("Usage: gfs_tool reclaim <mountpoint>\n");

	fd = open(argv[optind], O_RDONLY);
	if (fd < 0)
		die("can't open %s: %s\n", argv[optind], strerror(errno));

	check_for_gfs(fd, argv[optind]);

	if (!override) {
		printf("Don't do this if this file system is being exported by NFS (on any machine).\n");
		printf("\nAre you sure you want to proceed? [y/n] ");
		if (fgets(buf, 255, stdin) == NULL || buf[0] != 'y')
			die("aborted\n");

		printf("\n");
	}

	gi.gi_argc = 1;
	gi.gi_argv = gi_argv;
	gi.gi_data = buf;
	gi.gi_size = 256;

	if (ioctl(fd, GFS_IOCTL_SUPER, &gi) < 0)
		die("error doing do_reclaim: %s\n", strerror(errno));

	close(fd);

	printf("Reclaimed:\n");
	printf("%s", buf);
}

/**
 * do_shrink - shrink the inode cache for a filesystem
 * @argc:
 * @argv:
 *
 */

void
do_shrink(int argc, char **argv)
{
	int fd;
	char *gi_argv[] = { "do_shrink" };
	struct gfs_ioctl gi;

	if (optind == argc)
		die("Usage: gfs_tool shrink <mountpoint>\n");

	fd = open(argv[optind], O_RDONLY);
	if (fd < 0)
		die("can't open %s: %s\n",
		    argv[optind], strerror(errno));

	check_for_gfs(fd, argv[optind]);

	gi.gi_argc = 1;
	gi.gi_argv = gi_argv;

	if (ioctl(fd, GFS_IOCTL_SUPER, &gi))
		die("error doing ioctl: %s\n",
		    strerror(errno));

	close(fd);
}

/**
 * do_withdraw - freeze a GFS filesystem
 * @argc:
 * @argv:
 *
 */

void
do_withdraw(int argc, char **argv)
{
	char *cookie;
	int fd;
	char buf[256];
	int x;

	if (optind == argc)
		die("Usage: gfs_tool withdraw <mountpoint>\n");

	cookie = mp2cookie(argv[optind], FALSE);
	x = sprintf(buf, "withdraw %s\n", cookie);

	fd = open("/proc/fs/gfs", O_RDWR);
	if (fd < 0)
		die("can't open /proc/fs/gfs: %s\n",
		    strerror(errno));

	if (write(fd, buf, x) != x)
		die("can't write withdraw command: %s\n",
		    strerror(errno));
	if (read(fd, buf, 256))
		die("can't withdraw %s: %s\n",
		    argv[optind], strerror(errno));

	close(fd);
}
