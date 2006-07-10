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

#ifndef __GFS2_MKFS_DOT_H__
#define __GFS2_MKFS_DOT_H__

#include "linux_endian.h"
#include <linux/gfs2_ondisk.h>
#include "osi_list.h"
#include "copyright.cf"
#include "ondisk.h"

#define SRANDOM do { srandom(time(NULL) ^ getpid()); } while (0)
#define RESRANDOM do { srandom(RANDOM(1000000000)); } while (0)

/* main_grow */
void main_grow(int argc, char *argv[]);

/* main_jadd */
void main_jadd(int argc, char *argv[]);

/* main_mkfs */
void main_mkfs(int argc, char *argv[]);

/* main_shrink */
void main_shrink(int argc, char *argv[]);

static inline int __do_read(int fd, char *buff, size_t len, 
			    const char *file, int line)
{
	int ret = read(fd, buff, len);
	if (ret < 0) {
		die("bad read: %s on line %d of file %s\n", 
		    strerror(errno), line, file);
	}
	return ret;
}

#define do_read(fd, buf, len) \
	__do_read((fd), (buf), (len), __FILE__, __LINE__)

static inline int __do_write(int fd, char *buff, size_t len,
			     const char *file, int line)
{
	int ret = write(fd, buff, len);
	if (ret != len) {
		die("bad write: %s on line %d of file %s\n",
		    strerror(errno), line, file);
	}
	return ret;
}

#define do_write(fd, buf, len) \
	__do_write((fd), (buf), (len), __FILE__, __LINE__)

static inline int __do_lseek(int fd, off_t off, const char *file, int line)
{
	if (lseek(fd, off, SEEK_SET) != off) {
		die("bad seek: %s on line %d of file %s\n",
		    strerror(errno), line, file);
	}
	return 0;
}

#define do_lseek(fd, off) \
	__do_lseek((fd), (off), __FILE__, __LINE__)

#endif /* __GFS2_MKFS_DOT_H__ */
