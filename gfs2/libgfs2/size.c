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
#include <sys/mount.h>

#include "libgfs2.h"

#ifndef BLKGETSIZE64
#define BLKGETSIZE64 _IOR(0x12, 114, size_t)
#endif

/**
 * do_device_size - determine the size of a Linux block device
 * @device: the path to the device node
 *
 * Returns: -1 on error (with errno set), 0 on success (with @bytes set)
 */

static int
do_device_size(int fd, uint64_t *bytes)
{
	off_t off;
#if 0
	int error;
	unsigned long size;

	error = ioctl(fd, BLKGETSIZE64, bytes);	/* Size in bytes */
	if (!error)
		return 0;

	error = ioctl(fd, BLKGETSIZE, &size);	/* Size in 512-byte blocks */
	if (!error) {
		*bytes = ((uint64_t) size) << 9;
		return 0;
	}
#endif
	off = lseek(fd, 0, SEEK_END);
	if (off >= 0) {
		*bytes = off;
		return 0;
	}

	return -1;
}

/**
 * device_size - figure out a device's size
 * @fd: the file descriptor of a device
 * @bytes: the number of bytes the device holds
 *
 * Returns: -1 on error (with errno set), 0 on success (with @bytes set)
 */

int
device_size(int fd, uint64_t *bytes)
{
	struct stat st;
	int error;

	error = fstat(fd, &st);
	if (error)
		return error;

	if (S_ISREG(st.st_mode)) {
		*bytes = st.st_size;
		return 0;
	} else if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode))
		return do_device_size(fd, bytes);
	else if (S_ISDIR(st.st_mode))
		errno = EISDIR;
	else
		errno = EINVAL;

	return -1;
}
