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

#include "libgfs2.h"

/**
 * check_for_gfs - check to see if GFS is on this device
 * @fd: a file descriptor open on a device open for (at least) reading
 * @type: a buffer that contains the type of filesystem
 * @type_len: the amount of space pointed to by @type
 *
 * An EINVAL returned from lseek means that the device was too
 * small -- at least on Linux.
 *
 * Returns: -1 on error (with errno set), 1 if not GFS,
 *          0 if GFS found (with type set)
 */

static int
check_for_gfs(int fd, char *type, unsigned type_len)
{
	unsigned char buf[512];
	uint32_t *p = (uint32_t *)buf;
	int error;

	error = lseek(fd, 65536, SEEK_SET);
	if (error < 0)
		return (errno == EINVAL) ? 1 : error;
	else if (error != 65536) {
		errno = EINVAL;
		return -1;
	}

	error = read(fd, buf, 512);
	if (error < 0)
		return error;
	else if (error < 8)
		return 1;

	if (be32_to_cpu(*p) != 0x01161970 ||
	    be32_to_cpu(*(p + 1)) != 1)
		return 1;

	snprintf(type, type_len, "GFS filesystem");

	return 0;
}

/**
 * check_for_pool - check to see if Pool is on this device
 * @fd: a file descriptor open on a device open for (at least) reading
 * @type: a buffer that contains the type of filesystem
 * @type_len: the amount of space pointed to by @type
 *
 * Returns: -1 on error (with errno set), 1 if not Pool,
 *          0 if Pool found (with type set)
 */

static int
check_for_pool(int fd, char *type, unsigned type_len)
{
	unsigned char buf[512];
	uint64_t *p = (uint64_t *)buf;
	int error;

	error = lseek(fd, 0, SEEK_SET);
	if (error < 0)
		return error;
	else if (error != 0) {
		errno = EINVAL;
		return -1;
	}

	error = read(fd, buf, 512);
	if (error < 0)
		return error;
	else if (error < 8)
		return 1;

	if (be64_to_cpu(*p) != 0x11670)
		return 1;

	snprintf(type, type_len, "Pool subdevice");

	return 0;
}

/**
 * check_for_partition - check to see if Partition is on this device
 * @fd: a file descriptor open on a device open for (at least) reading
 * @type: a buffer that contains the type of filesystem
 * @type_len: the amount of space pointed to by @type
 *
 * Returns: -1 on error (with errno set), 1 if not Partition,
 *          0 if Partition found (with type set)
 */

static int
check_for_partition(int fd, char *type, unsigned type_len)
{
	unsigned char buf[512];
	int error;

	error = lseek(fd, 0, SEEK_SET);
	if (error < 0)
		return error;
	else if (error != 0) {
		errno = EINVAL;
		return -1;
	}

	error = read(fd, buf, 512);
	if (error < 0)
		return error;
	else if (error < 512)
		return 1;

	if (buf[510] != 0x55 || buf[511] != 0xAA)
		return 1;

	snprintf(type, type_len, "partition information");

	return 0;
}

/**
 * check_for_ext23 - check to see if EXT23 is on this device
 * @fd: a file descriptor open on a device open for (at least) reading
 * @type: a buffer that contains the type of filesystem
 * @type_len: the amount of space pointed to by @type
 *
 * An EINVAL returned from lseek means that the device was too
 * small -- at least on Linux.
 *
 * Returns: -1 on error (with errno set), 1 if not EXT23,
 *          0 if EXT23 found (with type set)
 */

static int
check_for_ext23(int fd, char *type, unsigned type_len)
{
	unsigned char buf[512];
	uint16_t *p = (uint16_t *)buf;
	int error;

	error = lseek(fd, 1024, SEEK_SET);
	if (error < 0)
		return (errno == EINVAL) ? 1 : error;
	else if (error != 1024) {
		errno = EINVAL;
		return -1;
	}

	error = read(fd, buf, 512);
	if (error < 0)
		return error;
	else if (error < 58)
		return 1;

	if (le16_to_cpu(p[28]) != 0xEF53)
		return 1;

	snprintf(type, type_len, "EXT2/3 filesystem");

	return 0;
}

/**
 * check_for_swap - check to see if SWAP is on this device
 * @fd: a file descriptor open on a device open for (at least) reading
 * @type: a buffer that contains the type of filesystem
 * @type_len: the amount of space pointed to by @type
 *
 * Returns: -1 on error (with errno set), 1 if not SWAP,
 *          0 if SWAP found (with type set)
 */

static int
check_for_swap(int fd, char *type, unsigned type_len)
{
	unsigned char buf[8192];
	int error;

	error = lseek(fd, 0, SEEK_SET);
	if (error < 0)
		return error;
	else if (error != 0) {
		errno = EINVAL;
		return -1;
	}

	error = read(fd, buf, 8192);
	if (error < 0)
		return error;
	else if (error < 4096)
		return 1;

	if (memcmp(buf + 4086, "SWAP-SPACE", 10) &&
	    memcmp(buf + 4086, "SWAPSPACE2", 10))
		return 1;

	snprintf(type, type_len, "swap device");

	return 0;
}

/**
 * check_for_lvm1 - check to see if LVM1 is on this device
 * @fd: a file descriptor open on a device open for (at least) reading
 * @type: a buffer that contains the type of filesystem
 * @type_len: the amount of space pointed to by @type
 *
 * Returns: -1 on error (with errno set), 1 if not LVM1,
 *          0 if LVM1 found (with type set)
 */

static int
check_for_lvm1(int fd, char *type, unsigned type_len)
{
	unsigned char buf[512];
	int error;

	error = lseek(fd, 0, SEEK_SET);
	if (error < 0)
		return error;
	else if (error != 0) {
		errno = EINVAL;
		return -1;
	}

	error = read(fd, buf, 512);
	if (error < 0)
		return error;
	else if (error < 2)
		return 1;

	if (buf[0] != 'H' || buf[1] != 'M')
		return 1;

	snprintf(type, type_len, "lvm1 subdevice");

	return 0;
}

/**
 * check_for_lvm2 - check to see if LVM2 is on this device
 * @fd: a file descriptor open on a device open for (at least) reading
 * @type: a buffer that contains the type of filesystem
 * @type_len: the amount of space pointed to by @type
 *
 * Returns: -1 on error (with errno set), 1 if not LVM2,
 *          0 if LVM1 found (with type set)
 */

static int
check_for_lvm2(int fd, char *type, unsigned type_len)
{
	char buf[512];
	int error;
	int i;

	/* LVM 2 labels can start in sectors 1-4 */

	for (i = 1; i < 5; i++) {
		error = lseek(fd, 512 * i, SEEK_SET);
		if (error < 0)
			return (errno == EINVAL) ? 1 : error;
		else if (error != 512 * i) {
			errno = EINVAL;
			return -1;
		}

		error = read(fd, buf, 512);
		if (error < 0)
			return error;
		else if (error < 32)
			return 1;

		if (strncmp(buf, "LABELONE", 8) != 0)
			continue;
		if (((uint64_t *) buf)[1] != i)
			continue;
		/* FIXME: should check the CRC of the label here */
		if (strncmp(&buf[24], "LVM2 001", 8) != 0)
			continue;

		snprintf(type, type_len, "lvm2 subdevice");

		return 0;
	}

	return 1;
}

/**
 * check_for_cidev - check to see if CIDEV is on this device
 * @fd: a file descriptor open on a device open for (at least) reading
 * @type: a buffer that contains the type of filesystem
 * @type_len: the amount of space pointed to by @type
 *
 * Returns: -1 on error (with errno set), 1 if not CIDEV,
 *          0 if CIDEV found (with type set)
 */

static int
check_for_cidev(int fd, char *type, unsigned type_len)
{
	unsigned char buf[512];
	uint32_t *p = (uint32_t *) buf;
	int error;

	error = lseek(fd, 0, SEEK_SET);
	if (error < 0)
		return error;
	else if (error != 0) {
		errno = EINVAL;
		return -1;
	}

	error = read(fd, buf, 512);
	if (error < 0)
		return error;
	else if (error < 4)
		return 1;

	if (be32_to_cpu(*p) != 0x47465341)
		return 1;

	snprintf(type, type_len, "CIDEV");

	return 0;
}

/**
 * check_for_cca - check to see if CCA is on this device
 * @fd: a file descriptor open on a device open for (at least) reading
 * @type: a buffer that contains the type of filesystem
 * @type_len: the amount of space pointed to by @type
 *
 * Returns: -1 on error (with errno set), 1 if not CCA,
 *          0 if CCA found (with type set)
 */

static int
check_for_cca(int fd, char *type, unsigned type_len)
{
	unsigned char buf[512];
	uint32_t *p = (uint32_t *) buf;
	int error;

	error = lseek(fd, 0, SEEK_SET);
	if (error < 0)
		return error;
	else if (error != 0) {
		errno = EINVAL;
		return -1;
	}

	error = read(fd, buf, 512);
	if (error < 0)
		return error;
	else if (error < 4)
		return 1;

	if (be32_to_cpu(*p) != 0x122473)
		return 1;

	snprintf(type, type_len, "CCA device");

	return 0;
}

/**
 * check_for_reiserfs - check to see if reisterfs is on this device
 * @fd: a file descriptor open on a device open for (at least) reading
 * @type: a buffer that contains the type of filesystem
 * @type_len: the amount of space pointed to by @type
 *
 * Returns: -1 on error (with errno set), 1 if not reiserfs,
 *          0 if CCA found (with type set)
 */

static int
check_for_reiserfs(int fd, char *type, unsigned type_len)
{
	unsigned int pass;
	uint64_t offset;
	char buf[512];
	int error;

	for (pass = 0; pass < 2; pass++) {
		offset = (pass) ? 65536 : 8192;

		error = lseek(fd, offset, SEEK_SET);
		if (error < 0)
			return (errno == EINVAL) ? 1 : error;
		else if (error != offset) {
			errno = EINVAL;
			return -1;
		}

		error = read(fd, buf, 512);
		if (error < 0)
			return error;
		else if (error < 62)
			return 1;

		if (strncmp(buf + 52, "ReIsErFs", 8) == 0 ||
		    strncmp(buf + 52, "ReIsEr2Fs", 9) == 0 ||
		    strncmp(buf + 52, "ReIsEr3Fs", 9) == 0) {
			snprintf(type, type_len, "Reiserfs filesystem");
			return 0;
		}
	}

	return 1;
}

/**
 * identify_device - figure out what's on a device
 * @fd: a file descriptor open on a device open for (at least) reading
 * @type: a buffer that contains the type of filesystem
 * @type_len: the amount of space pointed to by @type
 *
 * The offset of @fd will be changed by this function.
 * This routine will not write to the device.
 *
 * Returns: -1 on error (with errno set), 1 if unabled to identify,
 *          0 if device identified (with type set)
 */

int
identify_device(int fd, char *type, unsigned type_len)
{
	int error;

	if (!type || !type_len) {
		errno = EINVAL;
		return -1;
	}

	error = check_for_pool(fd, type, type_len);
	if (error <= 0)
		return error;

	error = check_for_lvm1(fd, type, type_len);
	if (error <= 0)
		return error;

	error = check_for_lvm2(fd, type, type_len);
	if (error <= 0)
		return error;

	error = check_for_cidev(fd, type, type_len);
	if (error <= 0)
		return error;

	error = check_for_cca(fd, type, type_len);
	if (error <= 0)
		return error;

	error = check_for_gfs(fd, type, type_len);
	if (error <= 0)
		return error;

	error = check_for_ext23(fd, type, type_len);
	if (error <= 0)
		return error;

	error = check_for_reiserfs(fd, type, type_len);
	if (error <= 0)
		return error;

	error = check_for_swap(fd, type, type_len);
	if (error <= 0)
		return error;

	error = check_for_partition(fd, type, type_len);
	if (error <= 0)
		return error;

	return 1;
}
