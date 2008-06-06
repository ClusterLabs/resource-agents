#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <time.h>
#include <sys/param.h>

#include "global.h"
#include "gfs_ondisk.h"
#include "osi_list.h"
#include "mkfs_gfs.h"
#include "libgfs.h"

/**
 * device_geometry - Get the size of a device
 * @comline: the command line
 * @device: the structure the geometry is returned in
 *
 */

void device_geometry(commandline_t *comline, mkfs_device_t *device)
{
	int fd;
	uint64 bytes;
	int error;

	fd = open(comline->device, O_RDONLY);
	if (fd < 0)
		die("can't open %s: %s\n", comline->device, strerror(errno));

	error = device_size(fd, &bytes);
	if (error)
		die("can't determine size of %s: %s\n", comline->device, strerror(errno));

	close(fd);

	if (comline->debug)
		printf("\nPartition size = %"PRIu64"\n", bytes >> 9);

	device->nsubdev = 1;

	type_zalloc(device->subdev, mkfs_subdevice_t, 1);

	device->subdev->start = 0;
	device->subdev->length = bytes >> 9;
}

/**
 * add_journals_to_device - carve space out of a mkfs_device_t to add journals
 * @comline: the command line arguments
 * @device: the mkfs_device_t
 *
 */

void add_journals_to_device(commandline_t *comline, mkfs_device_t *device)
{
	mkfs_subdevice_t *old;
	uint64 jsize;
	unsigned int x;

	MKFS_ASSERT(device->nsubdev == 1 &&
				!device->subdev->is_journal, );

	if (!comline->journals)
		die("No journals specified (use -j)\n");

	if (!comline->jsize)
		die("journal size is zero (use -J)\n");

	jsize = comline->jsize * (1 << 20) / GFS_BASIC_BLOCK;

	if (comline->journals * jsize > device->subdev->length)
		die("Partition too small for number/size of journals\n");

	old = device->subdev;

	device->nsubdev = comline->journals + 2;
	type_zalloc(device->subdev, mkfs_subdevice_t, device->nsubdev);
	
	device->subdev[0].start = old->start;
	device->subdev[0].length = (old->length - comline->journals * jsize) / 2;

	for (x = 1; x <= comline->journals; x++) {
		device->subdev[x].start = device->subdev[x - 1].start + device->subdev[x - 1].length;
		device->subdev[x].length = jsize;
		device->subdev[x].is_journal = TRUE;
	}

	device->subdev[x].start = device->subdev[x - 1].start + device->subdev[x - 1].length;
	device->subdev[x].length = device->subdev[0].length;

	free(old);
}

/**
 * fix_device_geometry - round off address and lengths and convert to FS blocks
 * @comline: the command line
 * @device: the description of the underlying device
 *
 */

void fix_device_geometry(commandline_t *comline, mkfs_device_t *device)
{
	unsigned int x;
	uint64 offset, len;
	uint32 bbsize = comline->bsize >> 9;
	
	if (comline->debug) {
		printf("\nDevice Geometry:  (in basic blocks)\n");
		for (x = 0; x < device->nsubdev; x++)
			printf("  SubDevice #%d:  %s:  start = %"PRIu64", len = %"PRIu64"\n",
				   x,
				   (device->subdev[x].is_journal) ? "journal" : "data",
				   device->subdev[x].start,
				   device->subdev[x].length);
	}

	/*  Make sure all the subdevices are aligned  */

	for (x = 0; x < device->nsubdev; x++) {
		offset = device->subdev[x].start;
		len = device->subdev[x].length;

		if (len < 100 * bbsize)
			die("subdevice %d is way too small (%"PRIu64" bytes)\n", x, len * GFS_BASIC_BLOCK); 

		if (offset % bbsize) {
			len -= bbsize - (offset % bbsize);
			offset += bbsize - (offset % bbsize);
		}

		device->subdev[x].start = offset / bbsize;
		device->subdev[x].length = len / bbsize;
	}

	if (comline->debug) {
		printf("\nDevice Geometry:  (in FS blocks)\n");
		for (x = 0; x < device->nsubdev; x++)
			printf("  SubDevice #%d:  %s:  start = %"PRIu64", len = %"PRIu64"\n",
				   x,
				   (device->subdev[x].is_journal) ? "journal" : "data",
				   device->subdev[x].start,
				   device->subdev[x].length);

		printf("\njournals = %u\n", comline->journals);
	}
}
