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
#include <errno.h>

#include "gfs2_mkfs.h"

char *prog_name;

/**
 * print_usage - print out usage information
 *
 */

static void
print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s [options] <device>\n", prog_name);
	printf("\n");
	printf("Options:\n");
	printf("\n");
	printf("  -b <bytes>       Filesystem block size\n");
	printf("  -c <MB>          Size of quota change file\n");
	printf("  -D               Enable debugging code\n");
	printf("  -h               Print this help, then exit\n");
	printf("  -J <MB>          Size of journals\n");
	printf("  -j <num>         Number of journals\n");
	printf("  -O               Don't ask for confirmation\n");
	printf("  -p <name>        Name of the locking protocol\n");
	printf("  -q               Don't print anything\n");
	printf("  -r <MB>          Resource Group Size\n");
	printf("  -t <name>        Name of the lock table\n");
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
		optchar = getopt(argc, argv, "b:c:DhJ:j:Op:qr:t:u:VX");

		switch (optchar) {
		case 'b':
			sdp->bsize = atoi(optarg);
			break;

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

		case 'O':
			sdp->override = TRUE;
			break;

		case 'p':
			if (strlen(optarg) >= GFS2_LOCKNAME_LEN)
				die("lock protocol name %s is too long\n",
				    optarg);
			strcpy(sdp->lockproto, optarg);
			break;

		case 'q':
			sdp->quiet = TRUE;
			break;

		case 'r':
			sdp->rgsize = atoi(optarg);
			break;

		case 't':
			if (strlen(optarg) >= GFS2_LOCKNAME_LEN)
				die("lock table name %s is too long\n", optarg);
			strcpy(sdp->locktable, optarg);
			break;

		case 'u':
			sdp->ulsize = atoi(optarg);
			break;

		case 'V':
			printf("gfs2_mkfs %s (built %s %s)\n", GFS2_RELEASE_NAME,
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
		sdp->device_name = argv[optind];
		optind++;
	} else
		die("no device specified (try -h for help)\n");

	if (optind < argc)
		die("Unrecognized option: %s\n", argv[optind]);

	if (sdp->debug) {
		printf("Command Line Arguments:\n");
		printf("  proto = %s\n", sdp->lockproto);
		printf("  table = %s\n", sdp->locktable);
		printf("  bsize = %u\n", sdp->bsize);
		printf("  journals = %u\n", sdp->journals);
		printf("  jsize = %u\n", sdp->jsize);
		printf("  rgsize = %u\n", sdp->rgsize);
		printf("  ulsize = %u\n", sdp->ulsize);
		printf("  qcsize = %u\n", sdp->qcsize);
		printf("  debug = %d\n", sdp->debug);
		printf("  device = %s\n", sdp->device_name);
	}
}

static void
verify_arguments(struct gfs2_sbd *sdp)
{
	unsigned int x;

	if (!sdp->expert)
		test_locking(sdp->lockproto, sdp->locktable);

	/* Block sizes must be a power of two from 512 to 65536 */

	for (x = 512; x; x <<= 1)
		if (x == sdp->bsize)
			break;

	if (!x || sdp->bsize > 65536)
		die("block size must be a power of two between 512 and 65536\n");

	/* Look at this!  Why can't we go bigger than 2GB? */
	if (sdp->expert) {
		if (1 > sdp->rgsize || sdp->rgsize > 2048)
			die("bad resource group size\n");
	} else {
		if (32 > sdp->rgsize || sdp->rgsize > 2048)
			die("bad resource group size\n");
	}

	if (!sdp->journals)
		die("no journals specified\n");

	if (!sdp->jsize || sdp->jsize > 1024)
		die("bad journal size\n");

	if (!sdp->ulsize || sdp->ulsize > 64)
		die("bad unlinked size\n");

	if (!sdp->qcsize || sdp->qcsize > 64)
		die("bad quota change size\n");
}

void
compute_constants(struct gfs2_sbd *sdp)
{
	uint32_t hash_blocks, ind_blocks, leaf_blocks;
	uint32_t tmp_blocks;
	unsigned int x;

	sdp->next_inum = 1;

	sdp->sb_addr = GFS2_SB_ADDR * GFS2_BASIC_BLOCK / sdp->bsize;
	sdp->bsize_shift = ffs(sdp->bsize) - 1;

	sdp->sd_fsb2bb_shift = sdp->bsize_shift -
		GFS2_BASIC_BLOCK_SHIFT;
	sdp->sd_fsb2bb = 1 << sdp->sd_fsb2bb_shift;
	sdp->sd_diptrs = (sdp->bsize - sizeof(struct gfs2_dinode)) /
		sizeof(uint64_t);
	sdp->sd_inptrs = (sdp->bsize - sizeof(struct gfs2_meta_header)) /
		sizeof(uint64_t);
	sdp->sd_jbsize = sdp->bsize - sizeof(struct gfs2_meta_header);
	sdp->sd_hash_bsize = sdp->bsize / 2;
	sdp->sd_hash_bsize_shift = sdp->bsize_shift - 1;
	sdp->sd_hash_ptrs = sdp->sd_hash_bsize / sizeof(uint64_t);

	/*  Compute maximum reservation required to add a entry to a directory  */

	hash_blocks = DIV_RU(sizeof(uint64_t) * (1 << GFS2_DIR_MAX_DEPTH),
			     sdp->sd_jbsize);

	ind_blocks = 0;
	for (tmp_blocks = hash_blocks; tmp_blocks > sdp->sd_diptrs;) {
		tmp_blocks = DIV_RU(tmp_blocks, sdp->sd_inptrs);
		ind_blocks += tmp_blocks;
	}

	leaf_blocks = 2 + GFS2_DIR_MAX_DEPTH;

	sdp->sd_max_dirres = hash_blocks + ind_blocks + leaf_blocks;

	sdp->sd_heightsize[0] = sdp->bsize - sizeof(struct gfs2_dinode);
	sdp->sd_heightsize[1] = sdp->bsize * sdp->sd_diptrs;
	for (x = 2;; x++) {
		uint64_t space, d;
		uint32_t m;

		space = sdp->sd_heightsize[x - 1] * sdp->sd_inptrs;
		d = space;
		m = do_div(d, sdp->sd_inptrs);

		if (d != sdp->sd_heightsize[x - 1] || m)
			break;
		sdp->sd_heightsize[x] = space;
	}
	sdp->sd_max_height = x;
	if (sdp->sd_max_height > GFS2_MAX_META_HEIGHT)
		die("bad constants (1)\n");

	sdp->sd_jheightsize[0] = sdp->bsize - sizeof(struct gfs2_dinode);
	sdp->sd_jheightsize[1] = sdp->sd_jbsize * sdp->sd_diptrs;
	for (x = 2;; x++) {
		uint64_t space, d;
		uint32_t m;

		space = sdp->sd_jheightsize[x - 1] * sdp->sd_inptrs;
		d = space;
		m = do_div(d, sdp->sd_inptrs);

		if (d != sdp->sd_jheightsize[x - 1] || m)
			break;
		sdp->sd_jheightsize[x] = space;
	}
	sdp->sd_max_jheight = x;
	if (sdp->sd_max_jheight > GFS2_MAX_META_HEIGHT)
		die("bad constants (2)\n");
}

/**
 * are_you_sure - protect lusers from themselves
 * @sdp: the command line
 *
 */

static void
are_you_sure(struct gfs2_sbd *sdp)
{
	char buf[1024];
	char input[32];
	int unknown;

	unknown = identify_device(sdp->fd, buf, 1024);
	if (unknown < 0)
		die("error identifying the contents of %s: %s\n",
		    sdp->device_name, strerror(errno));

	printf("This will destroy any data on %s.\n",
	       sdp->device_name);
	if (!unknown)
		printf("  It appears to contain a %s.\n",
		       buf);

	printf("\nAre you sure you want to proceed? [y/n] ");
	fgets(input, 32, stdin);

	if (input[0] != 'y')
		die("aborted\n");
	else
		printf("\n");
}

/**
 * print_results - print out summary information
 * @sdp: the command line
 *
 */

static void
print_results(struct gfs2_sbd *sdp)
{
	if (sdp->quiet)
		return;

	if (sdp->debug)
		printf("\n");

	if (sdp->expert)
		printf("Expert mode:               on\n");

	printf("Device:                    %s\n", sdp->device_name);

	printf("Blocksize:                 %u\n", sdp->bsize);
	printf("Filesystem Size:           %"PRIu64"\n", sdp->fssize);

	printf("Journals:                  %u\n", sdp->journals);
	printf("Resource Groups:           %"PRIu64"\n", sdp->rgrps);

	printf("Locking Protocol:          \"%s\"\n", sdp->lockproto);
	printf("Lock Table:                \"%s\"\n", sdp->locktable);

	if (sdp->debug) {
		printf("\n");
		printf("Spills:                    %u\n", sdp->spills);
	}

	printf("\n");
	printf("WARNING: The GFS2 ondisk format is not yet set in stone.\n");
	printf("         At some point in the future, it will change and\n");
	printf("         you will have to remake your filesystems\n");
}

/**
 * main - do everything
 * @argc:
 * @argv:
 *
 * Returns: 0 on success, non-0 on failure
 */

int
main(int argc, char *argv[])
{
	struct gfs2_sbd sbd;
	unsigned int x;

	prog_name = argv[0];
	SRANDOM;

	memset(&sbd, 0, sizeof(struct gfs2_sbd));
	sbd.bsize = MKFS_DEFAULT_BSIZE;
	sbd.jsize = MKFS_DEFAULT_JSIZE;
	sbd.rgsize = MKFS_DEFAULT_RGSIZE;
	sbd.ulsize = MKFS_DEFAULT_ULSIZE;
	sbd.qcsize = MKFS_DEFAULT_QCSIZE;
	sbd.time = time(NULL);
	osi_list_init(&sbd.rglist);
	osi_list_init(&sbd.buf_list);
	for (x = 0; x < BUF_HASH_SIZE; x++)
		osi_list_init(&sbd.buf_hash[x]);

	decode_arguments(argc, argv, &sbd);
	verify_arguments(&sbd);

	sbd.fd = open(sbd.device_name, O_RDWR);
	if (sbd.fd < 0)
		die("can't open device %s: %s\n",
		    sbd.device_name, strerror(errno));

	if (!sbd.override)
		are_you_sure(&sbd);

	compute_constants(&sbd);

	/* Get the device geometry */

	device_geometry(&sbd);
	fix_device_geometry(&sbd);

	/* Compute the resource group layouts */

	compute_rgrp_layout(&sbd);

	/* Build ondisk structures */

	build_rgrps(&sbd);
	build_master(&sbd);
	build_sb(&sbd);
	build_jindex(&sbd);
	build_per_node(&sbd);
	build_inum(&sbd);
	build_statfs(&sbd);
	build_rindex(&sbd);
	build_quota(&sbd);
	build_root(&sbd);

	do_init(&sbd);

	/* Cleanup */

	inode_put(sbd.master_dir);
	inode_put(sbd.inum_inode);
	inode_put(sbd.statfs_inode);
	bsync(&sbd);
	fsync(sbd.fd);
	close(sbd.fd);

	print_results(&sbd);

	return EXIT_SUCCESS;
}
