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

#ifndef __IDDEV_DOT_H__
#define __IDDEV_DOT_H__


/**
 * indentify_device - figure out what's on a device
 * @fd: a file descriptor open on a device open for (at least) reading
 * @type: a buffer that contains the type of filesystem
 * @type_len: the amount of space pointed to by @type
 *
 * The offset of @fd will be changed by the function.
 * This routine will not write to this device.
 *
 * Returns: -1 on error (with errno set), 1 if unabled to identify,
 *          0 if device identified (with @type set)
 */

int identify_device(int fd, char *type, unsigned type_len);


/**
 * device_size - figure out a device's size
 * @fd: the file descriptor of a device
 * @bytes: the number of bytes the device holds
 *
 * Returns: -1 on error (with errno set), 0 on success (with @bytes set)
 */

int device_size(int fd, uint64 *bytes);


#endif /* __IDDEV_DOT_H__ */

