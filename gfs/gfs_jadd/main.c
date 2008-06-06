#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>

#include "gfs_ondisk.h"
#define __user
#include "gfs_ioctl.h"
#include "osi_list.h"
#include "libgfs.h"

#include "copyright.cf"

#define BLOCK_END(buffer, type) ((buffer) + GFS_BASIC_BLOCK - sizeof(type))

struct rglist_entry {
	osi_list_t list;
	struct gfs_rindex ri;
	struct gfs_rgrp rg;
};

struct jilist_entry {
	osi_list_t list;
	struct gfs_jindex ji;
};

/*
 * verbose: 0 = no messages, 1 = normal, 2 = everything
 * test: 0 = normal, 1 = don't actually write data, but do everything else
 * fspath: path to root of mounted GFS filesystem
 * device: the device upon which the GFS filesystem is mounted
 * fsoptions: the mount options used
 * devsize: the size of the device (in filesystem blocks, rounded down)
 * fssize: the size of the filesystem (in filesystem blocks, rounded down)
 * override_device_size: if non-zero, this is used for the device size
 * number_of_journals: Number of journals to add to each fs
 * journal_size: The size of each journal, in MB
 * journal_size_blocks: The size of each journal in fs blocks
 */
static int verbose = 1;
static int test = 0;
static char fspath[4096];
static char device[1024];
static char fsoptions[4096];
static uint64_t devsize;
static uint64_t fssize;
static uint64_t override_device_size = 0;
static unsigned int number_of_journals = 1;
static uint64_t journal_size = 128;
static uint64_t journal_size_blocks;

/*
 * fs_sb: the superblock read from the mounted filesystem
 * rglist_current: list of resource groups currently making up the filesystem
 * jilist_current: list of current journals in the filesystem
 * jilist_new: where we put the new resource groups to be written
 */
static struct gfs_sb fs_sb;
static osi_list_decl(rglist_current);
static osi_list_decl(jilist_current);
static osi_list_decl(jilist_new);

/**
 * device_geometry - Find out the size of a block device
 * @device: The name of the device
 *
 * Returns: The size of the device in FS blocks
 */

static uint64_t
device_geometry(char *device)
{
	int fd;
	uint64_t bytes;
	int error;

	if (override_device_size)
		bytes = override_device_size;
	else {
		fd = open(device, O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "gfs_jadd: can't open %s: %s\n",
				device, strerror(errno));
			exit(EXIT_FAILURE);
		}

		error = device_size(fd, &bytes);
		if (error) {
			fprintf(stderr,
				"gfs_grow: can't determine size of %s: %s\n",
				device, strerror(errno));
			exit(EXIT_FAILURE);
		}

		close(fd);
	}

	return bytes >> fs_sb.sb_bsize_shift;
}

/**
 * jread - Read from journaled file using ioctl()
 * @fd: The fd to read from
 * @file: The file to read
 * @buf: The buffer to fill
 * @size: The amount of data to read
 * @offset: The offset to read from
 *
 * Returns: Error code, or amount of data read
 */

int
jread(int fd, char *file, void *buf, uint64_t size, uint64_t *offset)
{
	struct gfs_ioctl gi;
	char *argv[] = { "do_hfile_read", file };
	int error;

	gi.gi_argc = 2;
	gi.gi_argv = argv;
	gi.gi_data = buf;
	gi.gi_size = size;
	gi.gi_offset = *offset;

	error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
	if (error > 0)
		*offset += error;

	return error;
}

/**
 * jwrite - Write to journaled file using ioctl()
 * @fd: The fd to write to
 * @file: The file to write
 * @buf: The buffer to write
 * @size: The amount of data to write
 * @offset: The offset at which to write the data
 *
 * Returns: Error code, or the amount of data written
 */

int
jwrite(int fd, char *file, void *buf, uint64_t size, uint64_t *offset)
{
	struct gfs_ioctl gi;
	char *argv[] = { "do_hfile_write", file };
	int error;

	gi.gi_argc = 2;
	gi.gi_argv = argv;
	gi.gi_data = buf;
	gi.gi_size = size;
	gi.gi_offset = *offset;

	error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
	if (error > 0)
		*offset += error;

	return error;
}

/**
 * filesystem_size - Calculate the size of the filesystem
 *
 * Reads the lists of journals and resource groups in order to
 * work out where the last block of the filesystem is located.
 *
 * Returns: The calculated size
 */

static uint64_t
filesystem_size(void)
{
	osi_list_t *tmp, *head;
	struct rglist_entry *rgl;
	struct jilist_entry *jil;
	uint64_t size = 0;
	uint64_t extent;

	tmp = head = &rglist_current;
	for (;;) {
		tmp = tmp->next;
		if (tmp == head)
			break;
		rgl = osi_list_entry(tmp, struct rglist_entry, list);
		extent = rgl->ri.ri_addr + rgl->ri.ri_length + rgl->ri.ri_data;
		if (extent > size)
			size = extent;
	}

	tmp = head = &jilist_current;
	for (;;) {
		tmp = tmp->next;
		if (tmp == head)
			break;
		jil = osi_list_entry(tmp, struct jilist_entry, list);
		extent = jil->ji.ji_addr + jil->ji.ji_nsegment * fs_sb.sb_seg_size;
		if (extent > size)
			size = extent;
	}

	return size;
}

/**
 * gfs_jientry - Get journal index entry
 * @fd: The fd of the journal index
 * @offset: The offset at which the journal entry appears
 *
 * Reads a single entry from the journal index file and
 * adds it to the list of current journal entries.
 *
 * Returns: 1 on EOF, 0 otherwise
 */

static int
get_jientry(int fd, uint64_t *offset)
{
	char buffer[sizeof(struct gfs_jindex)];
	int len = jread(fd, "jindex", buffer,
			sizeof(struct gfs_jindex), offset);
	struct jilist_entry *jil;

	if (len != sizeof(struct gfs_jindex)) {
		if (len == 0)
			return 1;
		fprintf(stderr, "Erk! Read odd size from jindex (%d)\n", len);
		exit(EXIT_FAILURE);
	}
	if ((jil = malloc(sizeof(struct jilist_entry))) == NULL) {
		perror("jilist_entry");
		exit(EXIT_FAILURE);
	}
	memset(jil, 0, sizeof(struct jilist_entry));
	gfs_jindex_in(&jil->ji, buffer);
	osi_list_add(&jil->list, &jilist_current);
	return 0;
}

/**
 * read_journals - Read the whole journal index
 * @fs_fd: An fd for some file or directory within the mounted GFS filesystem
 *
 */

static void
read_journals(int fs_fd)
{
	uint64_t offset = 0;
	while (get_jientry(fs_fd, &offset) == 0)
		/* do nothing */;
}

/**
 * get_rgrp - Read a single rindex entry
 * @fd: The fd for the rindex file
 * @offset: The offset at which to read the rindex entry
 *
 * Reads a single rindex entry and adds it to the list of current
 * resource group entries.
 *
 * Returns: 1 on EOF, 0 otherwise
 */

static int
get_rgrp(int fd, uint64_t *offset)
{
	char buffer[sizeof(struct gfs_rindex)];
	int len = jread(fd, "rindex", buffer,
			sizeof(struct gfs_rindex), offset);
	struct rglist_entry *rgl;

	if (len != sizeof(struct gfs_rindex)) {
		if (len == 0)
			return 1;
		fprintf(stderr, "Erk! Read odd size from rindex (%d)\n", len);
		exit(EXIT_FAILURE);
	}
	if ((rgl = malloc(sizeof(struct rglist_entry))) == NULL) {
		perror("rglist_entry");
		exit(EXIT_FAILURE);
	}
	memset(rgl, 0, sizeof(struct rglist_entry));
	gfs_rindex_in(&rgl->ri, buffer);
	osi_list_add(&rgl->list, &rglist_current);
	return 0;
}

/**
 * read_rgrps - Reads the contents of the rindex file
 * @fs_fd: An fd for any file or directory within the mounted GFS filesytem
 *
 */

static void
read_rgrps(int fs_fd)
{
	uint64_t offset = 0;
	while (get_rgrp(fs_fd, &offset) == 0)
		/* do nothing */;
}

/**
 * write_a_block - Write a block to the current device
 * @where: The position to write the block (in filesystem blocks)
 *
 * Writes a single disk block to the device. It has a safety check which
 * prevents it writing to the device at a position within the control of
 * the active filesystem. 
 */

static void
write_a_block(uint64_t where, uint64_t seq)
{
	char buffer[4096];
	uint64_t fsoffset = where * (uint64_t) fs_sb.sb_bsize;
	int fd = open(device, O_RDWR);
	struct gfs_log_header lh;

	memset(&lh, 0, sizeof(struct gfs_log_header));

	lh.lh_header.mh_magic = GFS_MAGIC;
	lh.lh_header.mh_type = GFS_METATYPE_LH;
	lh.lh_header.mh_format = GFS_FORMAT_LH;
	lh.lh_flags = GFS_LOG_HEAD_UNMOUNT;
	lh.lh_first = where;
	lh.lh_sequence = seq;

	if (fd < 0) {
		perror(device);
		exit(EXIT_FAILURE);
	}
	if (where < fssize) {
		fprintf(stderr,
			"Sanity check failed: Caught trying to write to live filesystem!\n");
		exit(EXIT_FAILURE);
	}

	memset(buffer, 0, 4096);
	gfs_log_header_out(&lh, buffer);
	gfs_log_header_out(&lh, BLOCK_END(buffer, struct gfs_log_header));

	if (lseek(fd, fsoffset, SEEK_SET) != fsoffset) {
		perror(device);
		exit(EXIT_FAILURE);
	}
	if (write(fd, buffer, fs_sb.sb_bsize) != fs_sb.sb_bsize) {
		perror("write_a_block");
		exit(EXIT_FAILURE);
	}
	close(fd);
}

/**
 * write_whole_journal - Write a complete journal to the disk
 * @jil: The information about the journal
 *
 * Write a complete new journal on disk.
 */

static void
write_whole_journal(struct jilist_entry *jil)
{
	uint64_t seg;
	uint64_t offset;

	for (seg = 0; seg < jil->ji.ji_nsegment; seg++) {
		offset = seg * fs_sb.sb_seg_size;
		write_a_block(jil->ji.ji_addr + offset, offset);
	}

	sync();
}

/**
 * get_length - Use stat() to get the length of a file
 * @fd: The fd of the file whose length we wish to know
 *
 * Returns: The length
 */

static uint64_t
get_length(int fd, char *file)
{
	struct gfs_ioctl gi;
	char *argv[] = { "get_hfile_stat", file };
	struct gfs_dinode di;
	int error;

	gi.gi_argc = 2;
	gi.gi_argv = argv;
	gi.gi_data = (char *)&di;
	gi.gi_size = sizeof(struct gfs_dinode);

	error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
	if (error != gi.gi_size) {
		perror("stat");
		fprintf(stderr, "Failed to get size of file. Aborting.\n");
		exit(EXIT_FAILURE);
	}

	return di.di_size;
}

/**
 * write_jindex - Writes new records to the end of the jindex file
 * @fs_fd: A fd of any file or directory withint the GFS filesystem
 *
 * This is the critical function in adding journals. It does the
 * actual write to the jindex which causes the GFS filesystem to see the
 * new journals which were previously added.
 */

static void
write_jindex(int fs_fd)
{
	osi_list_t *tmp, *head;
	struct jilist_entry *jil;
	char buffer[sizeof(struct gfs_jindex)];
	uint64_t offset;

	offset = get_length(fs_fd, "jindex");

	/*
	 * This is the critical section.
	 * If things mess up here, it could be very difficult to put right
	 */
	tmp = head = &jilist_new;
	for (;;) {
		tmp = tmp->next;
		if (tmp == head)
			break;
		jil = osi_list_entry(tmp, struct jilist_entry, list);
		gfs_jindex_out(&jil->ji, buffer);
		if (jwrite(fs_fd, "jindex", buffer,
			   sizeof(struct gfs_jindex), &offset) !=
		    sizeof(struct gfs_jindex)) {
			perror("write: jindex");
			fprintf(stderr, "Aborting...\n");
			exit(EXIT_FAILURE);
		}
	}
	/*
	 * This is the end of the critical section
	 */
}

/**
 * write_journals - Write the new journals to disk
 * @fs_fd: An fd from any file or directory on the GFS mounted filesystem
 *
 * This first writes out the new journal information to the
 * area of the disk beyond the area the filesystem is currently
 * using and then calls write_jindex() to make the filesystem see
 * the newly written journal.
 */

static void
write_journals(int fs_fd)
{
	osi_list_t *tmp, *head;
	struct jilist_entry *jil;

	tmp = head = &jilist_new;
	for (;;) {
		tmp = tmp->next;
		if (tmp == head)
			break;
		jil = osi_list_entry(tmp, struct jilist_entry, list);
		write_whole_journal(jil);
	}

	sync();
	sync();
	sync();

	write_jindex(fs_fd);

	sync();
	sync();
	sync();
}

/**
 * gather_info - Gathers all the information about the existing filesystem
 *
 */

static void
gather_info(void)
{
	int fd;
	struct gfs_ioctl gi;
	char *argv[] = { "get_super" };
	int error;

	fd = open(fspath, O_RDONLY);
	if (fd < 0) {
		perror(fspath);
		exit(EXIT_FAILURE);
	}

	gi.gi_argc = 1;
	gi.gi_argv = argv;
	gi.gi_data = (char *)&fs_sb;
	gi.gi_size = sizeof(struct gfs_sb);

	error = ioctl(fd, GFS_IOCTL_SUPER, &gi);
	if (error != gi.gi_size) {
		perror("ioctl: GFS_GET_SUPER");
		exit(EXIT_FAILURE);
	}

	read_rgrps(fd);
	read_journals(fd);
	close(fd);
	devsize = device_geometry(device);
	fssize = filesystem_size();

	journal_size_blocks = journal_size << (20 - fs_sb.sb_bsize_shift);
	/*
	 * Round size down to integer number of segments
	 */
	while (journal_size_blocks % fs_sb.sb_seg_size) {
		journal_size_blocks--;
		journal_size -= fs_sb.sb_bsize;
	}
}

/**
 * print_rgrps - Print information about resource groups
 * @lh: The list of resource groups to print
 *
 */

static void
print_rgrps(osi_list_t *lh)
{
	osi_list_t *tmp, *head;
	struct rglist_entry *rgl;
	int n = 0;

	tmp = head = lh;
	for (;;) {
		tmp = tmp->next;
		if (tmp == head)
			break;
		rgl = osi_list_entry(tmp, struct rglist_entry, list);
		n++;
		printf("RI: Addr %"PRIu64", RgLen %u, Start %"PRIu64", DataLen %u, BmapLen %u\n",
		       rgl->ri.ri_addr, rgl->ri.ri_length,
		       rgl->ri.ri_data1, rgl->ri.ri_data, rgl->ri.ri_bitbytes);
	}
	printf("RGRP: %d Resource groups in total\n", n);
}

/**
 * print_journals - Print a list of journals
 *
 */

static void
print_journals(osi_list_t *lh)
{
	osi_list_t *tmp, *head;
	struct jilist_entry *jil;
	int n = 0;

	tmp = head = lh;
	for (;;) {
		tmp = tmp->next;
		if (tmp == head)
			break;
		jil = osi_list_entry(tmp, struct jilist_entry, list);
		n++;
		printf("JI: Addr %"PRIu64" NumSeg %u SegSize %u\n",
		       jil->ji.ji_addr, jil->ji.ji_nsegment, fs_sb.sb_seg_size);
	}
	printf("JRNL: %d Journals in total\n", n);
}

/**
 * print_info - Print out various bits of (interesting?) information
 *
 */

static void
print_info(void)
{
	printf("FS: Mount Point: %s\n", fspath);
	printf("FS: Device: %s\n", device);
	printf("FS: Options: %s\n", fsoptions);
	printf("FS: Size: %"PRIu64"\n", fssize);
	if (verbose > 1) {
		printf("RGRP: Current Resource Group List:\n");
		print_rgrps(&rglist_current);
		printf("JRNL: Current Journal List:\n");
		print_journals(&jilist_current);
	}
	printf("DEV: Size: %"PRIu64"\n", devsize);
	if (verbose > 1) {
		printf("JRNL: New Journal List:\n");
		print_journals(&jilist_new);
	}
}

/**
 * make_journal - Make a new jilist_entry
 * @offset: The offset at which the new journal will go
 * @size: The size of the new journal in fs blocks
 *
 */

uint64_t
make_journal(uint64_t offset, uint64_t size)
{
	struct jilist_entry *jil = malloc(sizeof(struct jilist_entry));
	if (jil == NULL) {
		perror("jilist_entry");
		exit(EXIT_FAILURE);
	}
	memset(jil, 0, sizeof(struct jilist_entry));

	if (offset % fs_sb.sb_seg_size) {
		size -= fs_sb.sb_seg_size - (offset % fs_sb.sb_seg_size);
		offset += fs_sb.sb_seg_size - (offset % fs_sb.sb_seg_size);
	}

	jil->ji.ji_addr = offset;
	jil->ji.ji_nsegment = size / fs_sb.sb_seg_size;

	osi_list_add(&jil->list, &jilist_new);
	return offset + size;
}

/**
 * create_journals - Create a list of the new journals
 * 
 */

static int
create_journals(void)
{
	uint64_t offset = fssize;
	int n;

	if ((journal_size_blocks * number_of_journals) > (devsize - fssize)) {
		fprintf(stderr,
			"Requested size (%" PRIu64
			" blocks) greater than available space (%" PRIu64
			" blocks)\n", journal_size_blocks * number_of_journals,
			devsize - fssize);
		return -1;
	}

	for (n = 0; n < number_of_journals; n++)
		offset = make_journal(offset, journal_size_blocks);

	if (offset > devsize) {
		fprintf(stderr, "Calculation error: Out of bounds\n");
		exit(EXIT_FAILURE);
	}

	return 0;
}

/**
 * update_fs - Actually perform the filesystem update
 *
 */

static void
update_fs(void)
{
	int fd = open(fspath, O_RDONLY);
	if (fd < 0) {
		perror(fspath);
		exit(EXIT_FAILURE);
	}
	if (verbose)
		printf("Preparing to write new FS information...\n");
	write_journals(fd);
	if (verbose)
		printf("Done.\n");
	close(fd);
}

/**
 * find_fs - Find the filesystem which the user specified
 * @name: The name of a device or mount point
 *
 * Returns: 0 if the filesystem is located, 1 otherwise
 */

static int
find_fs(char *name)
{
	FILE *fp = fopen("/proc/mounts", "r");
	char buffer[4096];
	char fstype[80];
	int fsdump, fspass;

	if (fp == NULL) {
		perror("open: /proc/mounts");
		exit(EXIT_FAILURE);
	}
	while ((fgets(buffer, 4095, fp)) != NULL) {
		buffer[4095] = 0;
		if (strstr(buffer, name) == 0)
			continue;
		if (sscanf(buffer, "%s %s %s %s %d %d", device, fspath, fstype,
			   fsoptions, &fsdump, &fspass) != 6)
			continue;
		if (strcmp(fstype, "gfs") != 0)
			continue;
		if ((strcmp(device, name) != 0) && (strcmp(fspath, name) != 0))
			continue;
		fclose(fp);
		return 0;
	}
	fprintf(stderr, "GFS Filesystem %s not found\n", name);
	fclose(fp);
	return 1;
}

/**
 * delete_rgrp_list - Delete a list of rgrps
 * @list: The list to delete
 *
 */

static void
delete_rgrp_list(osi_list_t *list)
{
	struct rglist_entry *rg;

	while (!osi_list_empty(list)) {
		rg = osi_list_entry(list->next, struct rglist_entry, list);
		osi_list_del(&rg->list);
		free(rg);
	}
}

/**
 * delete_jrnl_list - Delete a list of journals
 * @list: The list to delete
 *
 */

static void
delete_jrnl_list(osi_list_t *list)
{
	struct jilist_entry *ji;

	while (!osi_list_empty(list)) {
		ji = osi_list_entry(list->next, struct jilist_entry, list);
		osi_list_del(&ji->list);
		free(ji);
	}
}

/**
 * usage - Print out the usage message
 *
 * This function does not include documentation for the -D option
 * since normal users have no use for it at all. The -D option is
 * only for developers. It intended use is in combination with the
 * -T flag to find out what the result would be of trying different
 * device sizes without actually having to try them manually.
 */

static void
usage(void)
{
	fprintf(stdout,
		"Usage:\n"
		"\n"
		"gfs_jadd [options] /path/to/filesystem\n"
		"\n"
		"Options:\n"
		"  -h               Print this usage information.\n"
		"  -J <MB>          Size of journals in MB (minimum 32, default 128)\n"
		"  -j <num>         Number of journals to add (default 1)\n"
		"  -q               Quiet, reduce verbosity\n"
		"  -T               Test, do everything except update FS\n"
		"  -V               Version information\n"
		"  -v               Verbose, increase verbosity\n");
}

/**
 * main - Tha main function
 * @argc: The argument count
 * @argv: The argument vector
 *
 * Runs through the filesystem expansion code for each of the specified
 * filesystems. Each filesystem specified on the command line has the
 * same options applied to it. You'll need to run the program multiple times
 * if you want to use it on several different filesystems with different
 * options for each. If you forget to specify a filesystem, then it is
 * assumed that the program has run successfully, since its done everything
 * asked of it, and it exits without printing a message.
 *
 * Returns: 0 on success, -1 otherwise
 */

int
main(int argc, char *argv[])
{
	int opt;
	int error = 0;

	while ((opt = getopt(argc, argv, "D:Vhj:J:vqT?")) != EOF) {
		switch (opt) {
		case 'D':	/* This option is for testing only */
			override_device_size = atoi(optarg);
			override_device_size <<= 20;
			break;
		case 'V':
			printf("%s %s (built %s %s)\n", argv[0],
			       RELEASE_VERSION, __DATE__, __TIME__);
			printf("%s\n", REDHAT_COPYRIGHT);
			exit(0);
		case 'h':
			usage();
			exit(0);
		case 'j':
			number_of_journals = atoi(optarg);
			if (number_of_journals < 1) {
				fprintf(stderr,
					"Erk! Number of journals must be 1 or greater.\n");
				usage();
				exit(EXIT_FAILURE);
			}
			break;
		case 'J':
			journal_size = atoi(optarg);
			if (journal_size < 32) {
				fprintf(stderr,
					"Erk! Specified journal size of %"
					PRIu64 " is too small.\n",
					journal_size);
				usage();
				exit(EXIT_FAILURE);
			}
			break;
		case 'q':
			if (verbose)
				verbose--;
			break;
		case 'T':
			test = 1;
			break;
		case 'v':
			verbose++;
			break;
		case ':':
		case '?':
			fprintf(stderr, "Please use '-h' for usage.\n");
			exit(EXIT_FAILURE);
		default:
			fprintf(stderr, "Bad programmer! You forgot"
				" to catch the %c flag\n", opt);
			exit(EXIT_FAILURE);
			break;
		}
	}

	if (optind == argc) {
		usage();
		exit(EXIT_FAILURE);
	}

	while ((argc - optind) > 0) {
		if (find_fs(argv[optind++])) {
			error = 1;
			continue;
		}
		gather_info();
		if (fssize > devsize) {
			fprintf(stderr,
				"Filesystem thinks device is bigger than it really is.... skipping\n");
			error = 1;
			continue;
		}
		if (create_journals()) {
			error = 1;
			continue;
		}
		if (verbose)
			print_info();
		if (!test)
			update_fs();
		delete_rgrp_list(&rglist_current);
		delete_jrnl_list(&jilist_current);
		delete_jrnl_list(&jilist_new);
	}

	return error;
}
