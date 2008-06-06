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
 */
static int verbose = 1;
static int test = 0;
static char fspath[4096];
static char device[1024];
static char fsoptions[4096];
static uint64_t devsize;
static uint64_t fssize;
static uint64_t override_device_size = 0;

/*
 * fs_sb: the superblock read from the mounted filesystem
 * rglist_current: list of resource groups currently making up the filesystem
 * rglist_new: where we put the new resource groups to be written
 * jilist_current: list of current journals in the filesystem
 */
static struct gfs_sb fs_sb;
static osi_list_decl(rglist_current);
static osi_list_decl(rglist_new);
static osi_list_decl(jilist_current);

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
			fprintf(stderr, "gfs_grow: can't open %s: %s\n",
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
 * @rg: The (optional) resource group to write
 *
 * Writes a single disk block to the device. It has a safety check which
 * prevents it writing to the device at a position within the control of
 * the active filesystem. If @rg is NULL, it writes a single block of
 * zeros with a meta_header, otherwise the resource group is copied 
 * into the start of the block.
 */

static void
write_a_block(uint64_t where, struct gfs_rgrp *rg)
{
	char buffer[4096];
	uint64_t fsoffset = where * (uint64_t) fs_sb.sb_bsize;
	int fd = open(device, O_RDWR);
	struct gfs_meta_header mh;
	mh.mh_magic = GFS_MAGIC;
	mh.mh_type = GFS_METATYPE_RB;
	mh.mh_format = GFS_FORMAT_RB;

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
	if (rg)
		gfs_rgrp_out(rg, buffer);
	else
		gfs_meta_header_out(&mh, buffer);
	if (lseek(fd, fsoffset, SEEK_SET) != fsoffset) {
		perror(device);
		exit(EXIT_FAILURE);
	}
	if (write(fd, buffer, fs_sb.sb_bsize) != fs_sb.sb_bsize) {
		perror("write_zero_block");
		exit(EXIT_FAILURE);
	}
	close(fd);
}

/**
 * write_whole_rgrp - Write a complete rgrp, including bitmaps
 * @rgl: The information about the resource group
 *
 * Writes a complete rgrp, including any bitmap blocks required
 * by calling write_a_block() a number of times. Calls sync() to
 * ensure data really reached disk.
 */

static void
write_whole_rgrp(struct rglist_entry *rgl)
{
	uint32_t l;
	uint32_t nzb = rgl->ri.ri_length;
	uint64_t addr = rgl->ri.ri_addr;

	write_a_block(addr++, &rgl->rg);
	for (l = 1; l < nzb; l++)
		write_a_block(addr++, NULL);
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
 * write_rindex - Writes new records to the end of the rindex file
 * @fs_fd: A fd of any file or directory withint the GFS filesystem
 *
 * This is the critical function in expanding a filesystem. It does the
 * actual write to the rindex which causes the GFS filesystem to see the
 * new resource groups which were previously added.
 */

static void
write_rindex(int fs_fd)
{
	osi_list_t *tmp, *head;
	struct rglist_entry *rgl;
	char buffer[sizeof(struct gfs_rindex)];
	uint64_t offset;

	offset = get_length(fs_fd, "rindex");

	/*
	 * This is the critical section.
	 * If things mess up here, it could be very difficult to put right
	 */
	tmp = head = &rglist_new;
	for (;;) {
		tmp = tmp->next;
		if (tmp == head)
			break;
		rgl = osi_list_entry(tmp, struct rglist_entry, list);
		gfs_rindex_out(&rgl->ri, buffer);
		if (jwrite(fs_fd, "rindex", buffer,
			   sizeof(struct gfs_rindex), &offset) !=
		    sizeof(struct gfs_rindex)) {
			perror("write: rindex");
			fprintf(stderr, "Aborting...\n");
			exit(EXIT_FAILURE);
		}
	}
	/*
	 * This is the end of the critical section
	 */
}

/**
 * write_rgrps - Write the new resource groups to disk
 * @fs_fd: An fd from any file or directory on the GFS mounted filesystem
 *
 * This first writes out the new resource group information to the
 * area of the disk beyond the area the filesystem is currently
 * using and then calls write_rindex() to make the filesystem see
 * the newly written resource groups.
 */

static void
write_rgrps(int fs_fd)
{
	osi_list_t *tmp, *head;
	struct rglist_entry *rgl;

	tmp = head = &rglist_new;
	for (;;) {
		tmp = tmp->next;
		if (tmp == head)
			break;
		rgl = osi_list_entry(tmp, struct rglist_entry, list);
		write_whole_rgrp(rgl);
	}

	sync();
	sync();
	sync();

	write_rindex(fs_fd);

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
		printf("RGRP: New Resource Group List:\n");
		print_rgrps(&rglist_new);
	}
}

#define RGRP_STUFFED_BLKS(sb) (((sb)->sb_bsize - sizeof(struct gfs_rgrp)) * GFS_NBBY)
#define RGRP_BITMAP_BLKS(sb) (((sb)->sb_bsize - sizeof(struct gfs_meta_header)) * GFS_NBBY)

/**
 * rgrp_length - Calculate the length of a resource group
 * @size: The total size of the resource group
 *
 */

uint64_t
rgrp_length(uint64_t size)
{
	uint64_t bitbytes = RGRP_BITMAP_BLKS(&fs_sb) + 1;
	uint64_t stuff = RGRP_STUFFED_BLKS(&fs_sb) + 1;
	uint64_t blocks = 1;

	if (size < stuff)
		goto out;
	size -= stuff;
	while (size > bitbytes) {
		blocks++;
		size -= bitbytes;
	}
	if (size)
		blocks++;
 out:
	return blocks;
}

/**
 * make_rgrp - Make a new rglist_entry
 * @offset: The offset at which the new rgrp will go
 * @size: The size of the new rgrp
 *
 * Returns: The end of the new resource group
 */

uint64_t
make_rgrp(uint64_t offset, uint64_t size)
{
	struct rglist_entry *rgl = malloc(sizeof(struct rglist_entry));
	if (rgl == NULL)
		exit(EXIT_FAILURE);
	memset(rgl, 0, sizeof(struct rglist_entry));

	rgl->ri.ri_addr = offset;
	rgl->ri.ri_length = rgrp_length(size);
	rgl->ri.ri_data1 = offset + rgl->ri.ri_length;
	rgl->ri.ri_data = size - rgl->ri.ri_length;

	/* Round down to nearest multiple of GFS_NBBY */
	while (rgl->ri.ri_data & 0x03)
		rgl->ri.ri_data--;

	rgl->ri.ri_bitbytes = rgl->ri.ri_data / GFS_NBBY;

	rgl->rg.rg_header.mh_magic = GFS_MAGIC;
	rgl->rg.rg_header.mh_type = GFS_METATYPE_RG;
	rgl->rg.rg_header.mh_format = GFS_FORMAT_RG;
	rgl->rg.rg_free = rgl->ri.ri_data;

	osi_list_add_prev(&rgl->list, &rglist_new);
	return offset + size;
}

/**
 * create_rgrps - Create a list of the new rgrps
 * 
 */

static void
create_rgrps(void)
{
	uint64_t space = devsize - fssize;
	uint64_t optimal_rgrp_size = RGRP_STUFFED_BLKS(&fs_sb) +
		14 * RGRP_BITMAP_BLKS(&fs_sb) + 15;
	uint64_t rgrps = space / optimal_rgrp_size;
	uint64_t offset = fssize;
	uint64_t rgsize;
	uint64_t n;

	if (space % optimal_rgrp_size)
		rgrps++;
	rgsize = optimal_rgrp_size;

	for (n = 0; n < rgrps; n++)
		offset = make_rgrp(offset, (n != 0) ? rgsize :
				   (space - ((rgrps - 1) * rgsize)));

	if (offset > devsize) {
		fprintf(stderr, "Calculation error: Out of bounds\n");
		exit(EXIT_FAILURE);
	}
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
	write_rgrps(fd);
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
find_fs(const char *name)
{
	FILE *fp = fopen("/proc/mounts", "r");
	char buffer[4096];
	char fstype[80];
	int fsdump, fspass;
	char *realname;

	realname = realpath(name, NULL);
	if (!realname) {
		perror(name);
		return -1;
	}
	if (fp == NULL) {
		perror("open: /proc/mounts");
		exit(EXIT_FAILURE);
	}
	while ((fgets(buffer, 4095, fp)) != NULL) {
		buffer[4095] = 0;
		if (strstr(buffer, realname) == 0)
			continue;
		if (sscanf(buffer, "%s %s %s %s %d %d", device, fspath, fstype,
			   fsoptions, &fsdump, &fspass) != 6)
			continue;
		if (strcmp(fstype, "gfs") != 0)
			continue;
		if ((strcmp(device, realname) != 0) &&
		    (strcmp(fspath, realname) != 0))
			continue;
		fclose(fp);
		free(realname);
		return 0;
	}
	fprintf(stderr, "GFS Filesystem %s not found\n", name);
	fclose(fp);
	free(realname);
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
 * @list: the list to delete
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
		"gfs_grow [options] /path/to/filesystem\n"
		"\n"
		"Options:\n"
		"  -h               Usage information\n"
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

	while ((opt = getopt(argc, argv, "VD:hqTv?")) != EOF) {
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
			/* Unknown flag */
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
			error = 1;
			fprintf(stderr,
				"Filesystem thinks device is bigger than it really is.... skipping\n");
			continue;
		}
		if ((devsize - fssize) < 100) {
			error = 1;
			fprintf(stderr,
				"Device has grown by less than 100 blocks.... skipping\n");
			continue;
		}
		create_rgrps();
		if (verbose)
			print_info();
		if (!test)
			update_fs();
		delete_rgrp_list(&rglist_current);
		delete_rgrp_list(&rglist_new);
		delete_jrnl_list(&jilist_current);
	}

	return error;
}
