/*****************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef _FSCK_H
#define _FSCK_H


#include "fsck_incore.h"
#include "log.h"

struct gfs_sb;
struct fsck_sb;

#define NOT_IMPLEMENTED log_debug("Warning: %s (%s:%d) not implemented\n",\
			           __func__, __FILE__, __LINE__)

#define NEEDS_CHECKING log_debug("Warning: %s (%s:%d) needs verification\n", \
			         __func__, __FILE__, __LINE__)

/* from gfs_ondisk.h
#define GFS_NBBY                (4)
#define GFS_BIT_SIZE            (2)
#define GFS_BIT_MASK            (0x00000003)

#define GFS_BLKST_FREE          (0)
#define GFS_BLKST_USED          (1)
#define GFS_BLKST_FREEMETA      (2)
#define GFS_BLKST_USEDMETA      (3)
*/
/* Need a bitmap to store used blocks in */
/* number of fs blocks in fs / GFS_NBBY */
/* bleh - this is *huge* - how do we get around this? 128 MB/bitmap
 * for 2TB fs with 4k blocksize - 1024MB bitmap with 512byte blocksize */


struct options {
	char *device;
	int yes:1;
	int no:1;
};



int initialize(struct fsck_sb *sbp);
void destroy(struct fsck_sb *sbp);
int pass1(struct fsck_sb *sbp);
int pass1b(struct fsck_sb *sbp);
int pass1c(struct fsck_sb *sbp);
int pass2(struct fsck_sb *sbp, struct options *opts);
int pass3(struct fsck_sb *sbp, struct options *opts);
int pass4(struct fsck_sb *sbp, struct options *opts);
int pass5(struct fsck_sb *sbp, struct options *opts);

/* FIXME: Hack to get this going for pass2 - this should be pulled out
 * of pass1 and put somewhere else... */
int add_to_dir_list(struct fsck_sb *sbp, uint64_t block);

#endif /* _FSCK_H */
