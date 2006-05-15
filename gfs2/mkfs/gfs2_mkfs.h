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

#endif /* __GFS2_MKFS_DOT_H__ */
