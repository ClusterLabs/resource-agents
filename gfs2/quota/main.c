#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
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

#include <linux/types.h>
#include "gfs2_quota.h"

#define __user

#include "copyright.cf"


/*  Constants  */

#define OPTION_STRING ("bdf:g:hkl:mnsu:V")

char *prog_name;

/**
 * This function is for libgfs2's sake.
 */
void print_it(const char *label, const char *fmt, const char *fmt2, ...)
{
        va_list args;

        va_start(args, fmt2);
        printf("%s: ", label);
        vprintf(fmt, args);
        va_end(args);
}

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
	printf("  reset            reset the quota file\n");
	printf("\n");
	printf("Options:\n");
	printf("  -b               sizes are in FS blocks\n");
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

		case 'n':
			comline->numbers = TRUE;
			break;

		case 'V':
			printf("gfs2_quota %s (built %s %s)\n", RELEASE_VERSION,
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
		} else if (strcmp(argv[optind], "reset") == 0) {
			if (comline->operation)
				die("can't specify two operations\n");
			comline->operation = GQ_OP_RESET;
		} else
			die("unknown option %s\n", argv[optind]);

		optind++;
	}
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

static void
print_quota(commandline_t *comline,
	    int user, uint32_t id,
	    struct gfs2_quota *q,
	    struct gfs2_sb *sb)
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
			printf("limit: %-10llu warn: %-10lluvalue: %-10llu\n",
			       (unsigned long long)q->qu_limit / 2,
			       (unsigned long long)q->qu_warn / 2,
			       (unsigned long long)q->qu_value / 2);
		else
			printf("limit: %-10llu warn: %-10lluvalue: %-10llu\n",
			       (unsigned long long)
			       q->qu_limit << (sb->sb_bsize_shift - 10),
			       (unsigned long long)
			       q->qu_warn << (sb->sb_bsize_shift - 10),
			       (unsigned long long)
			       q->qu_value << (sb->sb_bsize_shift - 10));
		break;

	case GQ_UNITS_FSBLOCK:
		printf("limit: %-10llu warn: %-10llu value: %-10llu\n",
		       (unsigned long long)q->qu_limit,
		       (unsigned long long)q->qu_warn,
		       (unsigned long long)q->qu_value);
		break;

	case GQ_UNITS_BASICBLOCK:
		printf("limit: %-10llu warn: %-10llu value: %-10llu\n",
		       (unsigned long long)
		       q->qu_limit << (sb->sb_bsize_shift - 9),
		       (unsigned long long)
		       q->qu_warn << (sb->sb_bsize_shift - 9),
		       (unsigned long long)
		       q->qu_value << (sb->sb_bsize_shift - 9));
		break;

	default:
		die("bad units\n");
		break;
	}
}

void 
read_superblock(struct gfs2_sb *sb, struct gfs2_sbd *sdp)
{
	int fd;
	char buf[PATH_MAX];
	
	fd = open(sdp->device_name, O_RDONLY);
	if (fd < 0) {
		die("Could not open the block device %s: %s\n",
			sdp->device_name, strerror(errno));
	}
	do_lseek(fd, 0x10 * 4096);
	do_read(fd, buf, PATH_MAX);
	gfs2_sb_in(sb, buf);

	close(fd);
}

inline void 
read_quota_internal(int fd, uint32_t id, int id_type, struct gfs2_quota *q)
{
	/* seek to the appropriate offset in the quota file and read the 
	   quota info */
	uint64_t offset;
	char buf[256];
	int error;
	if (id_type == GQ_ID_USER)
		offset = (2 * (uint64_t)id) * sizeof(struct gfs2_quota);
	else
		offset = (2 * (uint64_t)id + 1) * sizeof(struct gfs2_quota);
	lseek(fd, offset, SEEK_SET);
	error = read(fd, buf, sizeof(struct gfs2_quota));
	if (error < 0)
		die("failed to read from quota file: %s\n", strerror(errno));
	if (error != sizeof(struct gfs2_quota))
		die("Couldn't read %lu bytes from quota file at offset %llu\n",
		    (unsigned long)sizeof(struct gfs2_quota),
		    (unsigned long long)offset);
	gfs2_quota_in(q, buf);
}

inline void 
write_quota_internal(int fd, uint32_t id, int id_type, struct gfs2_quota *q)
{
	/* seek to the appropriate offset in the quota file and read the
	   quota info */
	uint64_t offset;
	char buf[256];
	int error;
	if (id_type == GQ_ID_USER)
		offset = (2 * (uint64_t)id) * sizeof(struct gfs2_quota);
	else
		offset = (2 * (uint64_t)id + 1) * sizeof(struct gfs2_quota);
	lseek(fd, offset, SEEK_SET);
	gfs2_quota_out(q, buf);
	error = write(fd, buf, sizeof(struct gfs2_quota));
	if (error != sizeof(struct gfs2_quota))
		die("failed to write to quota file: %s\n", strerror(errno));
}

/**
 * get_last_quota_id - Get the last quota in the quota file
 * @fd: an open file descriptor of the quota file
 * @id_type: GQ_ID_USER or GQ_ID_GROUP
 * @max_id: return the maximum id obtained
 */
void 
get_last_quota_id(int fd, uint32_t *max_id)
{
	/* stat(2) the quota file to find how big it is. This will give us a
	 * a rough idea of what the last valid quota uid/gid is. It is possible 
	 * that the last quota in the file belongs to a group with gid:x and 
	 * the corresponding user quota with uid:x doesn't exist. In such cases
	 * we still return x as max_id. This max_id is ONLY A HINT. If used 
	 * as a terminating condition of a loop, another condition should also
	 * be specified.
	 */
	struct stat st;
	uint32_t qsize = sizeof(struct gfs2_quota);
	uint64_t size;
	if (fstat(fd, &st))
		die("failed to stat the quota file: %s\n", strerror(errno));
	size = st.st_size;
	if (!size)
		die("error: quota file is truncated to zero!\n");
	if (size % qsize) {
		printf("warning: quota file size not a multiple of "
		       "struct gfs2_quota\n");
		size = qsize * (size / qsize);
	}
	*max_id = (size - 1) / (2 * qsize);
}

/**
 * is_valid_quota_list - Check if we have a valid quota list
 * @fd: an open file descriptor of the quota file
 * Returns 0 or 1.
 */
int 
is_valid_quota_list(int fd)
{
	/* This is a slow test to determine if the quotas are in a 
	 * linked list. We should come up with something better
	 * Quota linked list format is identified by the following.
	 * step1: Get the maximum groupid and userid having valid
	 *        quotas in the quota file.
	 * step2: Obtain the size of the quota file. The size of the 
	 *        quota file (position of the last valid quota) 
	 *        determines the last user/group id.
	 * step3: If we can obtain the last valid quota through the 
	 *        lists, then our lists are good. Else, the lists are 
	 *        either corrupt or an older quota file format is in use
	 */
	int id_type = GQ_ID_GROUP;
	uint32_t id = 0, prev, ulast = 0, glast = 0, max;
	struct gfs2_quota q;

	get_last_quota_id(fd, &max);
again:
	do {
		read_quota_internal(fd, id, id_type, &q);
		prev = id;
		id = q.qu_ll_next;
		if (id > max)
			return 0;
	} while (id && id > prev);

	if (id && id <= prev)
		return 0;

	if (id_type == GQ_ID_GROUP)
		glast = prev;
	else
		ulast = prev;

	if (id_type == GQ_ID_GROUP) {
		id_type = GQ_ID_USER;
		id = 0;
		goto again;
	}

	if (glast != max && ulast != max)
		return 0;
	
	return 1;
}

void 
print_quota_list_warning()
{
	printf("\nWarning: This filesystem doesn't seem to have the new quota "
	       "list format or the quota list is corrupt. list, check and init "
	       "operation performance will suffer due to this. It is recommended "
	       "that you run the 'gfs2_quota reset' operation to reset the quota "
	       "file. All current quota information will be lost and you will "
	       "have to reassign all quota limits and warnings\n\n"); 
}

/**
 * adjust_quota_list - Adjust the quota linked list
 * @fd: The quota file descriptor
 * @comline: the struct containing the parsed command line arguments
 */
static void
adjust_quota_list(int fd, commandline_t *comline)
{
	uint32_t prev = 0, next = 0, id = comline->id;
	struct gfs2_quota tmpq, q;
	int id_type = comline->id_type;
	
	if (id == 0) /* root quota, don't do anything */
		goto out;
	/* We just wrote the quota for id in do_set(). Get it */
	next = 0;
	do {
		read_quota_internal(fd, next, id_type, &q);
		prev = next;
		next = q.qu_ll_next;
		if (prev == id) /* no duplicates, bail */
			goto out;
		if (prev < id && id < next) /* gotcha! */
			break;
	} while(next && next > prev);
	read_quota_internal(fd, id, id_type, &tmpq);
	tmpq.qu_ll_next = next;
	q.qu_ll_next = id;
	write_quota_internal(fd, id, id_type, &tmpq);
	write_quota_internal(fd, prev, id_type, &q);

out:
	return;
}

/**
 * do_reset - Reset all the quota data for a filesystem
 * @comline: the struct containing the parsed command line arguments
 */

static void
do_reset(struct gfs2_sbd *sdp, commandline_t *comline)
{
	int fd;
	char quota_file[BUF_SIZE], c;
	struct gfs2_quota q;

	if (!*comline->filesystem)
		die("need a filesystem to work on\n");

	printf("This operation will permanently erase all quota information. "
	       "You will have to re-assign all quota limit/warn values. "
	       "Proceed [y/N]? ");
	c = getchar();
	if (c != 'y' && c != 'Y')
		return;

	strcpy(sdp->path_name, comline->filesystem);
	check_for_gfs2(sdp);
	read_superblock(&sdp->sd_sb, sdp);
	if (!find_gfs2_meta(sdp))
		mount_gfs2_meta(sdp);
	lock_for_admin(sdp);
	
	strcpy(quota_file, sdp->metafs_path);
	strcat(quota_file, "/quota");

	fd = open(quota_file, O_RDWR);
	if (fd < 0) {
		close(sdp->metafs_fd);
		cleanup_metafs(sdp);
		die("can't open file %s: %s\n", quota_file,
		    strerror(errno));
	}

	read_quota_internal(fd, 0, GQ_ID_USER, &q);
	q.qu_ll_next = 0;
	write_quota_internal(fd, 0, GQ_ID_USER, &q);

	read_quota_internal(fd, 0, GQ_ID_GROUP, &q);
	q.qu_ll_next = 0;
	write_quota_internal(fd, 0, GQ_ID_GROUP, &q);

	/* truncate the quota file such that only the first
	 * two quotas(uid=0 and gid=0) remain.
	 */
	if (ftruncate(fd, (sizeof(struct gfs2_quota)) * 2))
		die("couldn't truncate quota file %s\n", strerror(errno));
	
	close(fd);
	close(sdp->metafs_fd);
	cleanup_metafs(sdp);
}

/**
 * do_list - List all the quota data for a filesystem
 * @comline: the struct containing the parsed command line arguments
 *
 */

static void 
do_list(struct gfs2_sbd *sdp, commandline_t *comline)
{
	int fd;
	struct gfs2_quota q;
	char buf[sizeof(struct gfs2_quota)];
	uint64_t offset;
	uint32_t id, prev, maxid;
	int pass = 0;
	int error = 0;
	char quota_file[BUF_SIZE];
	int id_type = comline->id_type;
	
	if (!*comline->filesystem)
		die("need a filesystem to work on\n");

	strcpy(sdp->path_name, comline->filesystem);
	check_for_gfs2(sdp);
	read_superblock(&sdp->sd_sb, sdp);
	if (!find_gfs2_meta(sdp))
		mount_gfs2_meta(sdp);
	lock_for_admin(sdp);
	
	strcpy(quota_file, sdp->metafs_path);
	strcat(quota_file, "/quota");

	fd = open(quota_file, O_RDONLY);
	if (fd < 0) {
		close(sdp->metafs_fd);
		cleanup_metafs(sdp);
		die("can't open file %s: %s\n", quota_file,
		    strerror(errno));
	}
	
	if (!is_valid_quota_list(fd)) {
		print_quota_list_warning();
		goto do_old_school;
	}
	get_last_quota_id(fd, &maxid);
	
	for (pass=0; pass<2; pass++) {
		id = 0;
		id_type = pass ? GQ_ID_GROUP : GQ_ID_USER;
		
		do {
			read_quota_internal(fd, id, id_type, &q);
			prev = id;
			if (q.qu_limit || q.qu_warn || q.qu_value)
				print_quota(comline, 
					    id_type == GQ_ID_USER ? TRUE : FALSE, 
					    id, &q, &sdp->sd_sb);
			id = q.qu_ll_next;
		} while(id && id > prev && id <= maxid);
	}
	goto out;

do_old_school:
	for (pass=0; pass<2; pass++) {
		if (!pass)
			offset = 0;
		else
			offset = sizeof(struct gfs2_quota);

		do {
			memset(buf, 0, sizeof(struct gfs2_quota));

			/* read hidden quota file here */
			lseek(fd, offset, SEEK_SET);
			error = read(fd, buf, sizeof(struct gfs2_quota));

			gfs2_quota_in(&q, buf);

			id = (offset / sizeof(struct gfs2_quota)) >> 1;

			if (q.qu_limit || q.qu_warn || q.qu_value)
				print_quota(comline, (pass) ? FALSE : TRUE, id,
					    &q, &sdp->sd_sb);

			offset += 2 * sizeof(struct gfs2_quota);
		} while (error == sizeof(struct gfs2_quota));
	}
out:
	close(fd);
	close(sdp->metafs_fd);
	cleanup_metafs(sdp);
}

/**
 * do_get_one - Get a quota value from one FS
 * @comline: the struct containing the parsed command line arguments
 * @filesystem: the filesystem to get from
 *
 */

static void 
do_get_one(struct gfs2_sbd *sdp, commandline_t *comline, char *filesystem)
{
	int fd;
	char buf[256];
	struct gfs2_quota q;
	uint64_t offset;
	int error;
	uint32_t maxid;
	char quota_file[BUF_SIZE];

	strcpy(sdp->path_name, filesystem);
	check_for_gfs2(sdp);
	read_superblock(&sdp->sd_sb, sdp);
	if (!find_gfs2_meta(sdp))
		mount_gfs2_meta(sdp);
	lock_for_admin(sdp);
	
	strcpy(quota_file, sdp->metafs_path);
	strcat(quota_file, "/quota");

	fd = open(quota_file, O_RDONLY);
	if (fd < 0) {
		close(sdp->metafs_fd);
		cleanup_metafs(sdp);
		die("can't open file %s: %s\n", quota_file,
		    strerror(errno));
	}

	if (comline->id_type == GQ_ID_USER)
		offset = (2 * (uint64_t)comline->id) * sizeof(struct gfs2_quota);
	else
		offset = (2 * (uint64_t)comline->id + 1) * sizeof(struct gfs2_quota);

	memset(&q, 0, sizeof(struct gfs2_quota));
	
	get_last_quota_id(fd, &maxid);
	if (comline->id > maxid)
		goto print_empty_quota;

	lseek(fd, offset, SEEK_SET);
	error = read(fd, buf, sizeof(struct gfs2_quota));
	if (error < 0) {
		close(fd);
		close(sdp->metafs_fd);
		cleanup_metafs(sdp);
		die("can't get quota info (%d): %s\n",
		    error, strerror(errno));
	}

	gfs2_quota_in(&q, buf);


print_empty_quota:
	print_quota(comline,
		    (comline->id_type == GQ_ID_USER), comline->id,
		    &q, &sdp->sd_sb);

	close(fd);
	close(sdp->metafs_fd);
	cleanup_metafs(sdp);	
}

/**
 * do_get - Get a quota value
 * @comline: the struct containing the parsed command line arguments
 *
 */

static void
do_get(struct gfs2_sbd *sdp, commandline_t *comline)
{
	int first = TRUE;

	if (*comline->filesystem)
		do_get_one(sdp, comline, comline->filesystem);
	else {
		char buf[256], device[256], path[256], type[256];
		FILE *file;

		file = fopen("/proc/mounts", "r");
		if (!file)
			die("can't open /proc/mounts: %s\n", strerror(errno));

		while (fgets(buf, 256, file)) {
			if (sscanf(buf, "%s %s %s", device, path, type) != 3)
				continue;
			if (strcmp(type, "gfs2") != 0)
				continue;

			if (first)
				first = FALSE;
			else
				printf("\n");

			printf("%s\n", path);
			do_get_one(sdp, comline, path);
		}

		fclose(file);
	}
}

/**
 * do_sync_one - sync the quotas on one GFS2 filesystem
 * @path: a file/directory in the filesystem
 *
 */
static void 
do_sync_one(struct gfs2_sbd *sdp, char *filesystem)
{
	char *fsname;

	fsname = mp2fsname(filesystem);
	set_sysfs(fsname, "quota_sync", "1");
}

/**
 * do_sync - sync out unsyned quotas
 * @comline: the struct containing the parsed command line arguments
 *
 */

void
do_sync(struct gfs2_sbd *sdp, commandline_t *comline)
{
	sync();

	if (*comline->filesystem)
		do_sync_one(sdp, comline->filesystem);
	else {
		char buf[256], device[256], path[256], type[256];
		FILE *file;

		file = fopen("/proc/mounts", "r");
		if (!file)
			die("can't open /proc/mounts: %s\n", strerror(errno));

		while (fgets(buf, 256, file)) {
			if (sscanf(buf, "%s %s %s", device, path, type) != 3)
				continue;
			if (strcmp(type, "gfs2") != 0)
				continue;

			do_sync_one(sdp, path);
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
do_set(struct gfs2_sbd *sdp, commandline_t *comline)
{
	int fd;
	uint64_t offset;
	uint64_t new_value;
	int error, adj_flag = 0;;
	char quota_file[BUF_SIZE];
	char id_str[16];
	struct stat stat_buf;
	char *fs;
	
	if (!*comline->filesystem)
		die("need a filesystem to work on\n");
	if (!comline->new_value_set)
		die("need a new value\n");

	strcpy(sdp->path_name, comline->filesystem);
	check_for_gfs2(sdp);
	read_superblock(&sdp->sd_sb, sdp);
	if (!find_gfs2_meta(sdp))
		mount_gfs2_meta(sdp);
	lock_for_admin(sdp);
	
	strcpy(quota_file, sdp->metafs_path);
	strcat(quota_file, "/quota");

	fd = open(quota_file, O_RDWR);
	if (fd < 0) {
		close(sdp->metafs_fd);
		cleanup_metafs(sdp);
		die("can't open file %s: %s\n", quota_file,
		    strerror(errno));
	}
	
	if (is_valid_quota_list(fd))
		adj_flag = 1;
	else
		print_quota_list_warning();

	switch (comline->id_type) {
	case GQ_ID_USER:
		offset = (2 * (uint64_t)comline->id) * sizeof(struct gfs2_quota);
		break;

	case GQ_ID_GROUP:
		offset = (2 * (uint64_t)comline->id + 1) * sizeof(struct gfs2_quota);
		break;

	default:
		fprintf(stderr, "invalid user/group ID\n");
		goto out;
	}

	switch (comline->units) {
	case GQ_UNITS_MEGABYTE:
		new_value =
			comline->new_value << (20 - sdp->sd_sb.sb_bsize_shift);
		break;

	case GQ_UNITS_KILOBYTE:
		if (sdp->sd_sb.sb_bsize == 512)
			new_value = comline->new_value * 2;
		else
			new_value = comline->new_value >>
				(sdp->sd_sb.sb_bsize_shift - 10);
		break;

	case GQ_UNITS_FSBLOCK:
		new_value = comline->new_value;
		break;

	case GQ_UNITS_BASICBLOCK:
		new_value = comline->new_value >>
			(sdp->sd_sb.sb_bsize_shift - 9);
		break;

	default:
		fprintf(stderr, "bad units\n");
		goto out;
	}

	new_value = cpu_to_be64(new_value);
	/*
	 * Hack to force writing the entire gfs2_quota structure to 
	 * the quota file instead of just the limit or warn values.
	 * This is because of a bug in gfs2 which doesn't extend 
	 * the quotafile appropriately to write the usage value of a 
	 * given id. For instance, if you write a limit value (8 bytes) 
	 * for userid x at offset 2*x, gfs2 will not extend the file and write 
	 * 8 bytes at offset (2*x + 16) when it has to update the usage 
	 * value for id x. Therefore, we extend the quota file to 
	 * a struct gfs2_quota boundary. i.e. The size of the quota file
	 * will always be a multiple of sizeof(struct gfs2_quota)
	 */
	if (fstat(fd, &stat_buf)) {
		fprintf(stderr, "stat failed: %s\n", strerror(errno));
		goto out;
	}
	if (stat_buf.st_size < (offset + sizeof(struct gfs2_quota))) {
		struct gfs2_quota tmp;
		memset((void*)(&tmp), 0, sizeof(struct gfs2_quota));
		switch (comline->operation) {
		case GQ_OP_LIMIT:
			tmp.qu_limit = new_value; break;
		case GQ_OP_WARN:
			tmp.qu_warn = new_value; break;
		}
		
		lseek(fd, offset, SEEK_SET);
		error = write(fd, (void*)(&tmp), sizeof(struct gfs2_quota));
		if (error != sizeof(struct gfs2_quota)) {
			fprintf(stderr, "can't write quota file (%d): %s\n", 
				error, strerror(errno));
			goto out;
		}
		/* Also, if the id type is USER, append another empty 
		 * struct gfs2_quota for the GROUP with the same id
		 */
		if (comline->id_type == GQ_ID_USER) {
			memset((void*)(&tmp), 0, sizeof(struct gfs2_quota));
			error = write(fd, (void*)(&tmp), sizeof(struct gfs2_quota));
			if (error != sizeof(struct gfs2_quota)) {
				fprintf(stderr, "can't write quota file (%d): %s\n", 
					error, strerror(errno));
				goto out;
			}
		}
	} else {
		switch (comline->operation) {
		case GQ_OP_LIMIT:
			offset += (unsigned long)(&((struct gfs2_quota *) NULL)->qu_limit);
			break;
			
		case GQ_OP_WARN:
			offset += (unsigned long)(&((struct gfs2_quota *) NULL)->qu_warn);
			break;
			
		default:
			fprintf(stderr, "invalid operation\n");
			goto out;
		};

		lseek(fd, offset, SEEK_SET);
		error = write(fd, (char*)&new_value, sizeof(uint64_t));
		if (error != sizeof(uint64_t)) {
			fprintf(stderr, "can't write quota file (%d): %s\n",
				error, strerror(errno));
			goto out;
		}
	}

	fs = mp2fsname(comline->filesystem);
	sprintf(id_str, "%d", comline->id);
	set_sysfs(fs, comline->id_type == GQ_ID_USER ?
		  "quota_refresh_user" : "quota_refresh_group", id_str);
	
	if (adj_flag)
		adjust_quota_list(fd, comline);
out:
	close(fd);
	close(sdp->metafs_fd);
	cleanup_metafs(sdp);
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
    struct gfs2_sbd sbd, *sdp = &sbd;
	commandline_t comline;

	prog_name = argv[0];
	sdp->metafs_mounted = 0;

	memset(sdp, 0, sizeof(struct gfs2_sbd));
	memset(&comline, 0, sizeof(commandline_t));

	decode_arguments(argc, argv, &comline);
	sdp->path_name = (char*) malloc(512);
	if (!sdp->path_name)
		die("Can't malloc! %s\n", strerror(errno));

	switch (comline.operation) {
	case GQ_OP_LIST:
		do_list(sdp, &comline);
		break;

	case GQ_OP_GET:
		do_get(sdp, &comline);
		break;

	case GQ_OP_LIMIT:
	case GQ_OP_WARN:
		do_set(sdp, &comline);
		break;

	case GQ_OP_SYNC:
		do_sync(sdp, &comline);
		break;

	case GQ_OP_CHECK:
		do_sync(sdp, &comline);
		do_check(sdp, &comline);
		break;

	case GQ_OP_INIT:
		do_sync(sdp, &comline);
		do_quota_init(sdp, &comline);
		break;

	case GQ_OP_RESET:
		do_reset(sdp, &comline);
		break;
	default:
		if (!comline.id_type) {
			comline.id_type = GQ_ID_USER;
			comline.id = geteuid();
		}
		do_get(sdp, &comline);
		break;
	}
	
	free(sdp->path_name);

	exit(EXIT_SUCCESS);
}
