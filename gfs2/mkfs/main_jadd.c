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
#include <time.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <errno.h>

#define __user
#include <linux/gfs2_ioctl.h>
#include <linux/gfs2_ondisk.h>

#include "gfs2_mkfs.h"

/**
 * print_usage - print out usage information
 *
 */

static void
print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s [options] /path/to/filesystem\n", prog_name);
	printf("\n");
	printf("Options:\n");
	printf("\n");
	printf("  -c <MB>          Size of quota change file\n");
	printf("  -D               Enable debugging code\n");
	printf("  -h               Print this help, then exit\n");
	printf("  -J <MB>          Size of journals\n");
	printf("  -j <num>         Number of journals\n");
	printf("  -q               Don't print anything\n");
	printf("  -T               Test, do everything except update FS\n");
	printf("  -u <MB>          Size of unlinked file\n");
	printf("  -V               Print program version information, then exit\n");
}

/**
 * decode_arguments - decode command line arguments and fill in the struct gfs2_sbd
 * @argc:
 * @argv:
 * @sdp: the decoded command line arguments
 *
 */

static void
decode_arguments(int argc, char *argv[], struct gfs2_sbd *sdp)
{
	int cont = TRUE;
	int optchar;

	while (cont) {
		optchar = getopt(argc, argv, "c:DhJ:j:qTu:VX");

		switch (optchar) {
		case 'c':
			sdp->qcsize = atoi(optarg);
			break;

		case 'D':
			sdp->debug = TRUE;
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case 'J':
			sdp->jsize = atoi(optarg);
			break;

		case 'j':
			sdp->journals = atoi(optarg);
			break;

		case 'q':
			sdp->quiet = TRUE;
			break;

		case 'T':
			sdp->test = TRUE;
			break;

		case 'u':
			sdp->utsize = atoi(optarg);
			break;

		case 'V':
			printf("gfs2_grow %s (built %s %s)\n", GFS2_RELEASE_NAME,
			       __DATE__, __TIME__);
			printf("%s\n", REDHAT_COPYRIGHT);
			exit(EXIT_SUCCESS);
			break;

		case 'X':
			sdp->expert = TRUE;
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

	if (optind < argc) {
		sdp->path_name = argv[optind];
		optind++;
	} else
		die("no path specified (try -h for help)\n");

	if (optind < argc)
		die("Unrecognized option: %s\n", argv[optind]);

	if (sdp->debug) {
		printf("Command Line Arguments:\n");
		printf("  qcsize = %u\n", sdp->qcsize);
		printf("  jsize = %u\n", sdp->jsize);
		printf("  journals = %u\n", sdp->journals);
		printf("  quiet = %d\n", sdp->quiet);
		printf("  test = %d\n", sdp->test);
		printf("  utsize = %u\n", sdp->utsize);
		printf("  path = %s\n", sdp->path_name);
	}
}

static void
verify_arguments(struct gfs2_sbd *sdp)
{
	if (!sdp->journals)
		die("no journals specified\n");

	if (sdp->jsize < 8 || sdp->jsize > 1024)
		die("bad journal size\n");

	if (!sdp->utsize || sdp->utsize > 64)
		die("bad unlinked size\n");

	if (!sdp->qcsize || sdp->qcsize > 64)
		die("bad quota change size\n");
}

/**
 * print_results - print out summary information
 * @sdp: the command line
 *
 */

static void
print_results(struct gfs2_sbd *sdp)
{
	if (sdp->debug)
		printf("\n");
	else if (sdp->quiet)
		return;

	if (sdp->test)
		printf("Test mode:                 on\n");
	if (sdp->expert)
		printf("Expert mode:               on\n");

	printf("Filesystem:                %s\n", sdp->path_name);
	printf("Old Journals               %u\n", sdp->orig_journals);
	printf("New Journals               %u\n", sdp->journals);

	if (sdp->test)
		printf("\nThe filesystem was not modified.\n");
}

int
create_new_inode(struct gfs2_sbd *sdp)
{
	char name[PATH_MAX];
	int fd;
	int error;

	error = snprintf(name, PATH_MAX, "%s/.gfs2_admin/new_inode", sdp->path_name);
	if (error >= PATH_MAX)
		die("create_new_inode (1)\n");

	for (;;) {
		fd = open(name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
		if (fd >= 0)
			break;
		if (errno == EEXIST) {
			error = unlink(name);
			if (error)
				die("can't unlink %s: %s\n",
				    name, strerror(errno));
		} else
			die("can't create %s: %s\n",
			    name, strerror(errno));
	}

	return fd;
}

void
add_ir(struct gfs2_sbd *sdp)
{
	int fd;
	char new_name[256];
	int error;

	fd = create_new_inode(sdp);

	{
		struct gfs2_inum_range ir;

		make_jdata(fd, "set");

		memset(&ir, 0, sizeof(struct gfs2_inum_range));
		error = write(fd, &ir, sizeof(struct gfs2_inum_range));
		if (error != sizeof(struct gfs2_inum_range))
			die("can't write to new_inode (%d): %s\n",
			    error, strerror(errno));
	}

	close(fd);

	sprintf(new_name, "inum_range%u", sdp->journals);
	error = rename2system(sdp, "per_node", new_name);
	if (error < 0 && errno != EEXIST)
		die("can't rename2system %s (%d): %s\n",
		    new_name, error, strerror(errno));
}

void
add_sc(struct gfs2_sbd *sdp)
{
	int fd;
	char new_name[256];
	int error;

	fd = create_new_inode(sdp);

	{
		struct gfs2_statfs_change sc;

		make_jdata(fd, "set");

		memset(&sc, 0, sizeof(struct gfs2_statfs_change));
		error = write(fd, &sc, sizeof(struct gfs2_statfs_change));
		if (error != sizeof(struct gfs2_statfs_change))
			die("can't write to new_inode (%d): %s\n",
			    error, strerror(errno));
	}

	close(fd);

	sprintf(new_name, "statfs_change%u", sdp->journals);
	error = rename2system(sdp, "per_node", new_name);
	if (error < 0 && errno != EEXIST)
		die("can't rename2system %s (%d): %s\n",
		    new_name, error, strerror(errno));
}

void
add_ut(struct gfs2_sbd *sdp)
{
	int fd;
	char new_name[256];
	int error;

	fd = create_new_inode(sdp);

	{
		char buf[sdp->bsize];
		unsigned int blocks = sdp->utsize << (20 - sdp->bsize_shift);
		unsigned int x;
		struct gfs2_meta_header mh;

		make_jdata(fd, "clear");

		memset(buf, 0, sdp->bsize);

		for (x = 0; x < blocks; x++) {
			error = write(fd, buf, sdp->bsize);
			if (error != sdp->bsize)
				die("can't write to new_inode (%d): %s\n",
				    error, strerror(errno));
		}

		error = lseek(fd, 0, SEEK_SET);
		if (error)
			die("can't lseek: %s\n", strerror(errno));

		memset(&mh, 0, sizeof(struct gfs2_meta_header));
		mh.mh_magic = GFS2_MAGIC;
		mh.mh_type = GFS2_METATYPE_UT;
		mh.mh_format = GFS2_FORMAT_UT;

		for (x = 0; x < blocks; x++) {
			mh.mh_blkno = bmap(fd, x);
			gfs2_meta_header_out(&mh, buf);

			error = write(fd, buf, sdp->bsize);
			if (error != sdp->bsize)
				die("can't write to new_inode (%d): %s\n",
				    error, strerror(errno));
		}

		error = fsync(fd);
		if (error)
			die("can't fsync: %s\n",
			    strerror(errno));
	}

	close(fd);

	sprintf(new_name, "unlinked_tag%u", sdp->journals);
	error = rename2system(sdp, "per_node", new_name);
	if (error < 0 && errno != EEXIST)
		die("can't rename2system %s (%d): %s\n",
		    new_name, error, strerror(errno));
}

void
add_qc(struct gfs2_sbd *sdp)
{
	int fd;
	char new_name[256];
	int error;

	fd = create_new_inode(sdp);

	{
		char buf[sdp->bsize];
		unsigned int blocks = sdp->qcsize << (20 - sdp->bsize_shift);
		unsigned int x;
		struct gfs2_meta_header mh;

		make_jdata(fd, "clear");

		memset(buf, 0, sdp->bsize);

		for (x = 0; x < blocks; x++) {
			error = write(fd, buf, sdp->bsize);
			if (error != sdp->bsize)
				die("can't write to new_inode (%d): %s\n",
				    error, strerror(errno));
		}

		error = lseek(fd, 0, SEEK_SET);
		if (error)
			die("can't lseek: %s\n", strerror(errno));

		memset(&mh, 0, sizeof(struct gfs2_meta_header));
		mh.mh_magic = GFS2_MAGIC;
		mh.mh_type = GFS2_METATYPE_QC;
		mh.mh_format = GFS2_FORMAT_QC;

		for (x = 0; x < blocks; x++) {
			mh.mh_blkno = bmap(fd, x);
			gfs2_meta_header_out(&mh, buf);

			error = write(fd, buf, sdp->bsize);
			if (error != sdp->bsize)
				die("can't write to new_inode (%d): %s\n",
				    error, strerror(errno));
		}

		error = fsync(fd);
		if (error)
			die("can't fsync: %s\n",
			    strerror(errno));
	}

	close(fd);

	sprintf(new_name, "quota_change%u", sdp->journals);
	error = rename2system(sdp, "per_node", new_name);
	if (error < 0 && errno != EEXIST)
		die("can't rename2system %s (%d): %s\n",
		    new_name, error, strerror(errno));
}

void
add_j(struct gfs2_sbd *sdp)
{
	int fd;
	char new_name[256];
	int error;

	fd = create_new_inode(sdp);

	{
		char buf[sdp->bsize];
		unsigned int blocks = sdp->jsize << (20 - sdp->bsize_shift);
		unsigned int x;
		struct gfs2_log_header lh;
		uint64_t seq = RANDOM(blocks);

		make_jdata(fd, "clear");

		memset(buf, 0, sdp->bsize);

		for (x = 0; x < blocks; x++) {
			error = write(fd, buf, sdp->bsize);
			if (error != sdp->bsize)
				die("can't write to new_inode (%d): %s\n",
				    error, strerror(errno));
		}

		error = lseek(fd, 0, SEEK_SET);
		if (error)
			die("can't lseek: %s\n", strerror(errno));

		memset(&lh, 0, sizeof(struct gfs2_log_header));
		lh.lh_header.mh_magic = GFS2_MAGIC;
		lh.lh_header.mh_type = GFS2_METATYPE_LH;
		lh.lh_header.mh_format = GFS2_FORMAT_LH;
		lh.lh_flags = GFS2_LOG_HEAD_UNMOUNT;

		for (x = 0; x < blocks; x++) {
			uint32_t hash;

			lh.lh_header.mh_blkno = bmap(fd, x);
			lh.lh_sequence = seq;
			lh.lh_blkno = x;
			gfs2_log_header_out(&lh, buf);
			hash = gfs2_disk_hash(buf, sizeof(struct gfs2_log_header));
			((struct gfs2_log_header *)buf)->lh_hash = cpu_to_le32(hash);

			error = write(fd, buf, sdp->bsize);
			if (error != sdp->bsize)
				die("can't write to new_inode (%d): %s\n",
				    error, strerror(errno));

			if (++seq == blocks)
				seq = 0;
		}

		error = fsync(fd);
		if (error)
			die("can't fsync: %s\n",
			    strerror(errno));
	}

	close(fd);

	sprintf(new_name, "journal%u", sdp->journals);
	error = rename2system(sdp, "jindex", new_name);
	if (error < 0 && errno != EEXIST)
		die("can't rename2system %s (%d): %s\n",
		    new_name, error, strerror(errno));
}

/**
 * main_jadd - do everything
 * @argc:
 * @argv:
 *
 */

void
main_jadd(int argc, char *argv[])
{
	struct gfs2_sbd sbd, *sdp = &sbd;
	unsigned int total;

	memset(sdp, 0, sizeof(struct gfs2_sbd));
	sdp->jsize = MKFS_DEFAULT_JSIZE;
	sdp->utsize = MKFS_DEFAULT_UTSIZE;
	sdp->qcsize = MKFS_DEFAULT_QCSIZE;

	decode_arguments(argc, argv, sdp);
	verify_arguments(sdp);

	sdp->path_fd = open(sdp->path_name, O_RDONLY);
	if (sdp->path_fd < 0)
		die("can't open root directory %s: %s\n",
		    sdp->path_name, strerror(errno));

	check_for_gfs2(sdp);
	lock_for_admin(sdp);

	find_block_size(sdp);
	compute_constants(sdp);
	find_current_journals(sdp);

	total = sdp->orig_journals + sdp->journals;
	for (sdp->journals = sdp->orig_journals;
	     sdp->journals < total;
	     sdp->journals++) {
		add_ir(sdp);
		add_sc(sdp);
		add_ut(sdp);
		add_qc(sdp);
		add_j(sdp);
	}

	close(sdp->path_fd);

	sync();

	print_results(sdp);
}
