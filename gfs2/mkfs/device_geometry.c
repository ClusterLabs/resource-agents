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
#include <errno.h>

#include "gfs2_mkfs.h"

/**
 * device_geometry - Get the size of a device
 * @w: the command line
 *
 */

void
device_geometry(struct gfs2_sbd *sdp)
{
	struct device *device = &sdp->device;
	uint64_t bytes;
	int error;

	error = device_size(sdp->fd, &bytes);
	if (error)
		die("can't determine size of %s: %s\n",
		    sdp->device_name, strerror(errno));

	if (sdp->debug)
		printf("\nPartition size = %"PRIu64"\n",
		       bytes >> GFS2_BASIC_BLOCK_SHIFT);

	device->nsubdev = 1;
	zalloc(device->subdev, sizeof(struct subdevice));

	device->subdev->start = 0;
	device->subdev->length = bytes >> GFS2_BASIC_BLOCK_SHIFT;
}

/**
 * fix_device_geometry - round off address and lengths and convert to FS blocks
 * @w: the command line
 *
 */

void
fix_device_geometry(struct gfs2_sbd *sdp)
{
	struct device *device = &sdp->device;
	unsigned int x;
	unsigned int bbsize = sdp->bsize >> GFS2_BASIC_BLOCK_SHIFT;
	uint64_t start, length;
	unsigned int remainder;

	if (sdp->debug) {
		printf("\nDevice Geometry:  (in basic blocks)\n");
		for (x = 0; x < device->nsubdev; x++)
			printf("  SubDevice #%u: start = %"PRIu64", length = %"PRIu64", rgf_flags = 0x%.8X\n",
			       x,
			       device->subdev[x].start,
			       device->subdev[x].length,
			       device->subdev[x].rgf_flags);
	}

	/* Make sure all the subdevices are aligned */

	for (x = 0; x < device->nsubdev; x++) {
		start = device->subdev[x].start;
		length = device->subdev[x].length;

		if (length < 1 << (20 - GFS2_BASIC_BLOCK_SHIFT))
			die("subdevice %d is way too small (%"PRIu64" bytes)\n",
			    x, length << GFS2_BASIC_BLOCK_SHIFT);

		remainder = start % bbsize;
		if (remainder) {
			length -= bbsize - remainder;
			start += bbsize - remainder;
		}

		device->subdev[x].start = start / bbsize;
		device->subdev[x].length = length / bbsize;
	}

	if (sdp->debug) {
		printf("\nDevice Geometry:  (in FS blocks)\n");
		for (x = 0; x < device->nsubdev; x++)
			printf("  SubDevice #%u: start = %"PRIu64", length = %"PRIu64", rgf_flags = 0x%.8X\n",
			       x,
			       device->subdev[x].start,
			       device->subdev[x].length,
			       device->subdev[x].rgf_flags);

		printf("\njournals = %u\n", sdp->journals);
	}
}

