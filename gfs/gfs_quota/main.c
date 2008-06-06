#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <time.h>
#include <limits.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <stdint.h>
#include <inttypes.h>

#include "linux_endian.h"
#include "gfs_ondisk.h"
#define __user
#include "gfs_ioctl.h"

#include "copyright.cf"

#include "gfs_quota.h"

/*  Constants  */

#define OPTION_STRING ("bdf:g:hkl:mnsu:V")

char *prog_name;

/**
 * print_usage - print usage info to the user
 *
 */

static void
print_usage()
{
	printf("Usage:\n");
	printf("\n");
	printf("%s <list|sync|get|limit|warn|check|init> [options]\n",
	       prog_name);
	printf("\n");
	printf("Actions:\n");
	printf("  list             list the whole quota file\n");
	printf("  sync             sync out unsynced quotas\n");
	printf("  get              get quota values for an ID\n");
	printf("  limit            set a quota limit value for an ID\n");
	printf("  warn             set a quota warning value for an ID\n");
	printf("  check            check the quota file\n");
	printf("  init             initialize the quota file\n");
	printf("\n");
	printf("Options:\n");
	printf("  -b               sizes are in FS blocks\n");
	printf("  -d               don't include hidden inode blocks\n");
	printf("  -f <directory>   the filesystem to work on\n");
	printf("  -g <gid>         get/set a group ID\n");
	printf("  -h               Print this help, then exit\n");
	printf("  -k               sizes are in KB\n");
	printf("  -l <size>        the new limit or warn value\n");
	printf("  -m               sizes are in MB\n");
	printf("  -n               print out UID/GID numbers instead of names\n");
	printf("  -s               sizes are in 512-byte blocks\n");
	printf("  -u <uid>         get/set a user ID\n");
	printf("  -V               Print program version information, then exit\n");
}

/**
 * check_for_gfs - Check to see if a descriptor is a file on a GFS filesystem
 * @fd: the file descriptor
 * @path: the path used to open the descriptor
 *
 */

void
check_for_gfs(int fd, char *path)
{
	unsigned int magic = 0;
	int error;

	error = ioctl(fd, GFS_IOCTL_IDENTIFY, &magic);
	if (error || magic != GFS_MAGIC)
		die("%s is not a GFS file/filesystem\n", path);
}

/**
 * do_get_super
 * fd:
 * sb:
 *
 */

void
do_get_super(int fd, struct gfs_sb *sb)
{
	struct gfs_ioctl gi;
	char *argv[] = { "get_super" };
	int error;

	gi.gi_argc = 1;
	gi.gi_argv = argv;
	gi.gi_data = (char *)sb;
	gi.gi_size = sizeof(struct gfs_sb);

	error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
	if (error != gi.gi_size)
		die("can't read the superblock (%d): %s\n",
		    error, strerror(errno));
}

/**
 * decode_arguments - parse command line arguments
 * @argc: well, it's argc...
 * @argv: well, it's argv...
 * @comline: the structure filled in with the parsed arguments
 *
 * Function description
 *
 * Returns: what is returned
 */

static void
decode_arguments(int argc, char *argv[], commandline_t *comline)
{
	int cont = TRUE;
	int optchar;

	while (cont) {
		optchar = getopt(argc, argv, OPTION_STRING);

		switch (optchar) {
		case 'u':
			comline->id_type = GQ_ID_USER;
			comline->id = name_to_id(TRUE, optarg, comline->numbers);
			break;

		case 'g':
			comline->id_type = GQ_ID_GROUP;
			comline->id = name_to_id(FALSE, optarg, comline->numbers);
			break;

		case 'l':
			if (!isdigit(*optarg))
				die("argument to -l must be a number\n");
			sscanf(optarg, "%"SCNu64, &comline->new_value);
			comline->new_value_set = TRUE;
			break;

		case 'f':
			if (!realpath(optarg, comline->filesystem))
				die("can't find %s: %s\n", optarg,
				    strerror(errno));
			break;

		case 'm':
			comline->units = GQ_UNITS_MEGABYTE;
			break;

		case 'k':
			comline->units = GQ_UNITS_KILOBYTE;
			break;

		case 'b':
			comline->units = GQ_UNITS_FSBLOCK;
			break;

		case 's':
			comline->units = GQ_UNITS_BASICBLOCK;
			break;

		case 'd':
			comline->no_hidden_file_blocks = TRUE;
			break;

		case 'n':
			comline->numbers = TRUE;
			break;

		case 'V':
			printf("gfs_quota %s (built %s %s)\n", RELEASE_VERSION,
			       __DATE__, __TIME__);
			printf("%s\n", REDHAT_COPYRIGHT);
			exit(EXIT_SUCCESS);
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case ':':
		case '?':
			fprintf(stderr, "Please use '-h' for usage.\n");
			exit(EXIT_FAILURE);
			break;

		case EOF:
			cont = FALSE;
			break;

		default:
			die("unknown option: %c\n", optchar);
			break;
		};
	}

	while (optind < argc) {
		if (strcmp(argv[optind], "list") == 0 ||
		    strcmp(argv[optind], "dump") == 0) {
			if (comline->operation)
				die("can't specify two operations\n");
			comline->operation = GQ_OP_LIST;
		} else if (strcmp(argv[optind], "sync") == 0) {
			if (comline->operation)
				die("can't specify two operations\n");
			comline->operation = GQ_OP_SYNC;
		} else if (strcmp(argv[optind], "get") == 0) {
			if (comline->operation)
				die("can't specify two operations\n");
			comline->operation = GQ_OP_GET;
		} else if (strcmp(argv[optind], "limit") == 0) {
			if (comline->operation)
				die("can't specify two operations\n");
			comline->operation = GQ_OP_LIMIT;
		} else if (strcmp(argv[optind], "warn") == 0) {
			if (comline->operation)
				die("can't specify two operations\n");
			comline->operation = GQ_OP_WARN;
		} else if (strcmp(argv[optind], "check") == 0) {
			if (comline->operation)
				die("can't specify two operations\n");
			comline->operation = GQ_OP_CHECK;
		} else if (strcmp(argv[optind], "init") == 0) {
			if (comline->operation)
				die("can't specify two operations\n");
			comline->operation = GQ_OP_INIT;
		} else
			die("unknown option %s\n", argv[optind]);

		optind++;
	}
}

/**
 * compute_hidden_blocks - figure out how much space the hidden inodes use
 * @comline: the struct containing the parsed command line arguments
 * @fd: the filedescriptor to the filesystem
 *
 * Returns: the number of hidden blocks
 */

uint64_t
compute_hidden_blocks(commandline_t *comline, int fd)
{
	struct gfs_dinode di;
	struct gfs_ioctl gi;
	uint64_t hidden_blocks = 0;
	int error;

	{
		char *argv[] = { "get_hfile_stat", "jindex" };

		gi.gi_argc = 2;
		gi.gi_argv = argv;	
		gi.gi_data = (char *)&di;
		gi.gi_size = sizeof(struct gfs_dinode);

		error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
		if (error != gi.gi_size)
			die("can't stat jindex (%d): %s\n",
			    error, strerror(errno));
		hidden_blocks += di.di_blocks;
	}

	{
		char *argv[] = { "get_hfile_stat", "rindex" };

		gi.gi_argc = 2;
		gi.gi_argv = argv;	
		gi.gi_data = (char *)&di;
		gi.gi_size = sizeof(struct gfs_dinode);

		error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
		if (error != gi.gi_size)
			die("can't stat rindex (%d): %s\n",
			    error, strerror(errno));
		hidden_blocks += di.di_blocks;
	}

	{
		char *argv[] = { "get_hfile_stat", "quota" };

		gi.gi_argc = 2;
		gi.gi_argv = argv;	
		gi.gi_data = (char *)&di;
		gi.gi_size = sizeof(struct gfs_dinode);

		error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
		if (error != gi.gi_size)
			die("can't stat quota file (%d): %s\n",
			    error, strerror(errno));
		hidden_blocks += di.di_blocks;
	}

	{
		char *argv[] = { "get_hfile_stat", "license" };

		gi.gi_argc = 2;
		gi.gi_argv = argv;	
		gi.gi_data = (char *)&di;
		gi.gi_size = sizeof(struct gfs_dinode);

		error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
		if (error != gi.gi_size)
			die("can't stat license file (%d): %s\n",
			    error, strerror(errno));
		hidden_blocks += di.di_blocks;
	}

	return hidden_blocks;
}

/**
 * print_quota - Print out a quota entry
 * @comline: the struct containing the parsed command line arguments
 * @user: TRUE if this is a user quota, FALSE if it's a group quota
 * @id: the ID
 * @q: the quota value
 * @sb: the superblock of the filesystem this quota belongs to
 *
 */

void
print_quota(commandline_t *comline,
	    int user, uint32_t id,
	    struct gfs_quota *q,
	    struct gfs_sb *sb)
{
	printf("%-5s %10s:  ", (user) ? "user" : "group",
	       id_to_name(user, id, comline->numbers));

	switch (comline->units) {
	case GQ_UNITS_MEGABYTE:
		printf("limit: %-10.1f warn: %-10.1f value: %-10.1f\n",
		       (double) q->qu_limit * sb->sb_bsize / 1048576,
		       (double) q->qu_warn * sb->sb_bsize / 1048576,
		       (double) q->qu_value * sb->sb_bsize / 1048576);
		break;

	case GQ_UNITS_KILOBYTE:
		if (sb->sb_bsize == 512)
			printf("limit: %-10"PRIu64" warn: %-10"PRIu64"value: %-10"PRId64"\n",
			       q->qu_limit / 2,
			       q->qu_warn / 2,
			       q->qu_value / 2);
		else
			printf("limit: %-10"PRIu64" warn: %-10"PRIu64"value: %-10"PRId64"\n",
			       q->qu_limit << (sb->sb_bsize_shift - 10),
			       q->qu_warn << (sb->sb_bsize_shift - 10),
			       q->qu_value << (sb->sb_bsize_shift - 10));
		break;

	case GQ_UNITS_FSBLOCK:
		printf("limit: %-10"PRIu64" warn: %-10"PRIu64" value: %-10"PRId64"\n",
		       q->qu_limit, q->qu_warn, q->qu_value);
		break;

	case GQ_UNITS_BASICBLOCK:
		printf("limit: %-10"PRIu64" warn: %-10"PRIu64" value: %-10"PRId64"\n",
		       q->qu_limit << (sb->sb_bsize_shift - 9),
		       q->qu_warn << (sb->sb_bsize_shift - 9),
		       q->qu_value << (sb->sb_bsize_shift - 9));
		break;

	default:
		die("bad units\n");
		break;
	}
}

/**
 * do_list - List all the quota data for a filesystem
 * @comline: the struct containing the parsed command line arguments
 *
 */

static void
do_list(commandline_t *comline)
{
	print_quota_file(comline);
}

/**
 * do_sync_one - sync the quotas on one GFS filesystem
 * @path: a file/directory in the filesystem
 *
 */

static void
do_sync_one(char *filesystem)
{
	int fd;
	char *argv[] = { "do_quota_sync" };
	struct gfs_ioctl gi;
	int error;

	fd = open(filesystem, O_RDONLY);
	if (fd < 0)
		die("can't open file %s: %s\n", filesystem, strerror(errno));

	check_for_gfs(fd, filesystem);

	gi.gi_argc = 1;
	gi.gi_argv = argv;

	error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
	if (error)
		die("can't sync quotas: %s\n", strerror(errno));

	close(fd);
}

/**
 * do_sync - sync out unsyned quotas
 * @comline: the struct containing the parsed command line arguments
 *
 */

void
do_sync(commandline_t *comline)
{
	sync();

	if (*comline->filesystem)
		do_sync_one(comline->filesystem);
	else {
		char buf[256], device[256], path[256], type[256];
		FILE *file;

		file = fopen("/proc/mounts", "r");
		if (!file)
			die("can't open /proc/mounts: %s\n", strerror(errno));

		while (fgets(buf, 256, file)) {
			if (sscanf(buf, "%s %s %s", device, path, type) != 3)
				continue;
			if (strcmp(type, "gfs") != 0)
				continue;

			do_sync_one(path);
		}

		fclose(file);
	}
}

/**
 * do_get_one - Get a quota value from one FS
 * @comline: the struct containing the parsed command line arguments
 * @filesystem: the filesystem to get from
 *
 */

static void
do_get_one(commandline_t *comline, char *filesystem)
{
	int fd;
	char buf[256];
	struct gfs_ioctl gi;
	struct gfs_quota q;
	struct gfs_sb sb;
	int error;

	fd = open(filesystem, O_RDONLY);
	if (fd < 0)
		die("can't open file %s: %s\n", comline->filesystem,
		    strerror(errno));

	check_for_gfs(fd, filesystem);

	sprintf(buf, "%s:%u",
		(comline->id_type == GQ_ID_USER) ? "u" : "g",
		comline->id);

	{
		char *argv[] = { "do_quota_read", buf };

		gi.gi_argc = 2;
		gi.gi_argv = argv;
		gi.gi_data = (char *)&q;
		gi.gi_size = sizeof(struct gfs_quota);

		memset(&q, 0, sizeof(struct gfs_quota));

		error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
		if (error < 0)
			die("can't get quota info (%d): %s\n",
			    error, strerror(errno));
	}

	if (comline->no_hidden_file_blocks && !comline->id)
		q.qu_value -= compute_hidden_blocks(comline, fd);

	do_get_super(fd, &sb);

	print_quota(comline,
		    (comline->id_type == GQ_ID_USER), comline->id,
		    &q, &sb);

	close(fd);
}

/**
 * do_get - Get a quota value
 * @comline: the struct containing the parsed command line arguments
 *
 */

static void
do_get(commandline_t *comline)
{
	int first = TRUE;

	if (*comline->filesystem)
		do_get_one(comline, comline->filesystem);
	else {
		char buf[256], device[256], path[256], type[256];
		FILE *file;

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

			printf("%s:\n", path);
			do_get_one(comline, path);
		}

		fclose(file);
	}
}

/**
 * do_set - Set a quota value
 * @comline: the struct containing the parsed command line arguments
 *
 */

static void
do_set(commandline_t *comline)
{
	int fd;
	uint64_t offset;
	struct gfs_ioctl gi;
	struct gfs_sb sb;
	uint64_t new_value;
	char buf[256];
	int error;

	if (!*comline->filesystem)
		die("need a filesystem to work on\n");
	if (!comline->new_value_set)
		die("need a new value\n");

	fd = open(comline->filesystem, O_RDONLY);
	if (fd < 0)
		die("can't open file %s: %s\n", comline->filesystem,
		    strerror(errno));

	check_for_gfs(fd, comline->filesystem);

	switch (comline->id_type) {
	case GQ_ID_USER:
		offset = (2 * (uint64_t)comline->id) * sizeof(struct gfs_quota);
		break;

	case GQ_ID_GROUP:
		offset = (2 * (uint64_t)comline->id + 1) * sizeof(struct gfs_quota);
		break;

	default:
		die("invalid user/group ID\n");
		break;
	}

	switch (comline->operation) {
	case GQ_OP_LIMIT:
		offset += (unsigned long)(&((struct gfs_quota *) NULL)->qu_limit);
		break;

	case GQ_OP_WARN:
		offset += (unsigned long)(&((struct gfs_quota *) NULL)->qu_warn);
		break;

	default:
		die("invalid operation\n");
		break;
	};

	do_get_super(fd, &sb);

	switch (comline->units) {
	case GQ_UNITS_MEGABYTE:
		new_value = comline->new_value << (20 - sb.sb_bsize_shift);
		break;

	case GQ_UNITS_KILOBYTE:
		if (sb.sb_bsize == 512)
			new_value = comline->new_value * 2;
		else
			new_value = comline->new_value >> (sb.sb_bsize_shift - 10);
		break;

	case GQ_UNITS_FSBLOCK:
		new_value = comline->new_value;
		break;

	case GQ_UNITS_BASICBLOCK:
		new_value = comline->new_value >> (sb.sb_bsize_shift - 9);
		break;

	default:
		die("bad units\n");
		break;
	}

	new_value = cpu_to_gfs64(new_value);

	{
		char *argv[] = { "do_hfile_write", "quota" };

		gi.gi_argc = 2;
		gi.gi_argv = argv;
		gi.gi_data = (char *)&new_value;
		gi.gi_size = sizeof(uint64_t);
		gi.gi_offset = offset;

		error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
		if (error != gi.gi_size)
			die("can't write quota file (%d): %s\n",
			    error, strerror(errno));
	}

	sprintf(buf, "%s:%u",
		(comline->id_type == GQ_ID_USER) ? "u" : "g",
		comline->id);

	{
		char *argv[] = { "do_quota_refresh", buf };

		gi.gi_argc = 2;
		gi.gi_argv = argv;

		error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
		if (error)
			die("can't refresh the quota LVB: %s\n",
			    strerror(errno));
	}

	close(fd);
}

/**
 * main - Do everything
 * @argc: well, it's argc...
 * @argv: well, it's argv...
 *
 * Returns: exit status
 */

int
main(int argc, char *argv[])
{
	commandline_t comline;

	prog_name = argv[0];

	memset(&comline, 0, sizeof(commandline_t));

	decode_arguments(argc, argv, &comline);

	switch (comline.operation) {
	case GQ_OP_LIST:
		do_list(&comline);
		break;

	case GQ_OP_SYNC:
		do_sync(&comline);
		break;

	case GQ_OP_GET:
		do_get(&comline);
		break;

	case GQ_OP_LIMIT:
	case GQ_OP_WARN:
		do_set(&comline);
		break;

	case GQ_OP_CHECK:
		do_sync(&comline);
		do_sync(&comline);
		do_check(&comline);
		break;

	case GQ_OP_INIT:
		do_sync(&comline);
		do_sync(&comline);
		do_init(&comline);
		break;

	default:
		if (!comline.id_type) {
			comline.id_type = GQ_ID_USER;
			comline.id = geteuid();
		}
		do_get(&comline);
		break;
	}

	exit(EXIT_SUCCESS);
}
