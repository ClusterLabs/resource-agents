#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>

#include "gfs2_debug.h"
#include "block_device.h"

/**
 * find_device_size -
 *
 */

void
find_device_size(void)
{
	device_size = lseek(device_fd, 0, SEEK_END);
	if (device_size < 0)
		die("can't determine device size: %s\n",
		    strerror(errno));
}

/**
 * get_block -
 * @bn:
 * @fatal:
 *
 * Returns: the data in the block (needs to be freed)
 */

char *
get_block(uint64_t bn, int fatal)
{
	char *data;

	if (device_size < (bn + 1) * block_size) {
		fprintf(stderr, "%s: block %"PRIu64" is off the end of the device\n",
			prog_name, bn);
		if (fatal)
			exit(EXIT_FAILURE);
	}

	data = malloc(block_size);
	if (!data)
		die("out of memory (%s, %u)\n",
		    __FILE__, __LINE__);

	do_lseek(device_fd, bn * block_size);
	do_read(device_fd, data, block_size);

	return data;
}

/**
 * print_size -
 *
 */

void
print_size(void)
{
	printf("%"PRIu64"\n", device_size);
}

/**
 * print_hexblock -
 *
 */

void
print_hexblock(void)
{
	char *data;
	unsigned int x;

	if (!block_size)
		die("no block size set\n");

	data = get_block(block_number, TRUE);

	for (x = 0; x < block_size; x++) {
		printf("%.2X", ((unsigned char *)data)[x]);
		if (x % 16 == 15)
			printf("\n");
		else
			printf(" ");
	}

	if (x % 16)
		printf("\n");

	free(data);
}

/**
 * print_rawblock -
 *
 */

void
print_rawblock(void)
{
	char *data;

	if (!block_size)
		die("no block size set\n");

	data = get_block(block_number, TRUE);
	do_write(STDOUT_FILENO, data, block_size);
	free(data);
}
