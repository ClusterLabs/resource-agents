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

#include <linux/types.h>
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
	printf("  -D               Enable debugging code\n");
	printf("  -h               Print this help, then exit\n");
	printf("  -q               Don't print anything\n");
	printf("  -r <MB>          Resource Group Size\n");
	printf("  -T               Test, do everything except update FS\n");
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
		optchar = getopt(argc, argv, "Dhqr:TVX");

		switch (optchar) {
		case 'D':
			sdp->debug = TRUE;
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case 'q':
			sdp->quiet = TRUE;
			break;

		case 'r':
			sdp->rgsize = atoi(optarg);
			break;

		case 'T':
			sdp->test = TRUE;
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
		printf("  quiet = %d\n", sdp->quiet);
		printf("  rgsize = %u\n", sdp->rgsize);
		printf("  test = %d\n", sdp->test);
		printf("  path = %s\n", sdp->path_name);
	}
}

static void
verify_arguments(struct gfs2_sbd *sdp)
{
	/* Look at this!  Why can't we go bigger than 2GB? */
	if (sdp->expert) {
		if (1 > sdp->rgsize || sdp->rgsize > 2048)
			die("bad resource group size\n");
	} else {
		if (32 > sdp->rgsize || sdp->rgsize > 2048)
			die("bad resource group size\n");
	}
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
	printf("Device:                    %s\n", sdp->device_name);
	printf("Blocksize:                 %u\n", sdp->bsize);

	printf("Old Filesystem Size:       %.2f GB (%"PRIu64" blocks)\n",
	       sdp->orig_fssize / ((float)(1 << 30)) * sdp->bsize, sdp->orig_fssize);
	printf("Old Resource Groups:       %"PRIu64"\n", sdp->orig_rgrps);

	printf("Device Size                %.2f GB (%"PRIu64" blocks)\n",
	       sdp->device_size / ((float)(1 << 30)) * sdp->bsize, sdp->device_size);

	printf("New Filesystem Size:       %.2f GB (%"PRIu64" blocks)\n",
	       sdp->fssize / ((float)(1 << 30)) * sdp->bsize, sdp->fssize);
	printf("New Resource Groups:       %"PRIu64"\n", sdp->rgrps);

	if (sdp->debug) {
		printf("\n");
		printf("Spills:                    %u\n", sdp->spills);
		printf("Writes:                    %u\n", sdp->writes);
	}

	if (sdp->test)
		printf("\nThe filesystem was not modified.\n");
}

/**
 * main - do everything
 * @argc:
 * @argv:
 *
 */

void
main_grow(int argc, char *argv[])
{
	struct gfs2_sbd sbd, *sdp = &sbd;
	unsigned int x;
	int error;

	memset(sdp, 0, sizeof(struct gfs2_sbd));
	sdp->rgsize = MKFS_DEFAULT_RGSIZE;
	osi_list_init(&sdp->rglist);
	osi_list_init(&sdp->buf_list);
	for (x = 0; x < BUF_HASH_SIZE; x++)
		osi_list_init(&sdp->buf_hash[x]);

	decode_arguments(argc, argv, sdp);
	verify_arguments(sdp);

	sdp->path_fd = open(sdp->path_name, O_RDONLY);
	if (sdp->path_fd < 0)
		die("can't open root directory %s: %s\n",
		    sdp->path_name, strerror(errno));

	check_for_gfs2(sdp);
	lock_for_admin(sdp);

	path2device(sdp);
	sdp->device_fd = open(sdp->device_name, O_RDWR);
	if (sdp->device_fd < 0)
		die("can't open device %s: %s\n",
		    sdp->device_name, strerror(errno));

	find_block_size(sdp);
	compute_constants(sdp);
	find_current_fssize(sdp);

	device_geometry(sdp);
	fix_device_geometry(sdp);

	if (sdp->device_size < sdp->orig_fssize)
		die("Did the device shrink?  "
		    "The device says it's %.2f GB, "
		    "but the filesystem thinks it takes up %.2f GB\n",
		    sdp->device_size / ((float)(1 << 30)) * sdp->bsize,
		    sdp->orig_fssize / ((float)(1 << 30)) * sdp->bsize);
	else if (sdp->device_size < sdp->orig_fssize +
		 (MKFS_MIN_GROW_SIZE << (20 - sdp->bsize_shift)))
		die("Cowardly refusing to grow the filesystem by %"PRIu64" blocks\n",
		    sdp->device_size - sdp->orig_fssize);

	munge_device_geometry_for_grow(sdp);

	compute_rgrp_layout(sdp, FALSE);
	build_rgrps(sdp);

	bsync(sdp);

	error = fsync(sdp->device_fd);
	if (error)
		die("can't fsync device (%d): %s\n",
		    error, strerror(errno));
	error = close(sdp->device_fd);
	if (error)
		die("error closing device (%d): %s\n",
		    error, strerror(errno));

	if (!sdp->test)
		add_to_rindex(sdp);

	statfs_sync(sdp);

	close(sdp->path_fd);

	print_results(sdp);
}
