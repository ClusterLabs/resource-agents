/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __TRANS_DOT_H__
#define __TRANS_DOT_H__

#define TRANS_IS_NEW            (53)
#define TRANS_IS_INCORE         (54)
void gfs_trans_print(struct gfs_sbd *sdp, struct gfs_trans *tr,
		     unsigned int where);

int gfs_trans_begin_i(struct gfs_sbd *sdp,
		      unsigned int meta_blocks, unsigned int extra_blocks,
		      char *file, unsigned int line);
#define gfs_trans_begin(sdp, mb, eb) \
gfs_trans_begin_i((sdp), (mb), (eb), __FILE__, __LINE__)

void gfs_trans_end(struct gfs_sbd *sdp);

void gfs_trans_add_gl(struct gfs_glock *gl);
void gfs_trans_add_bh(struct gfs_glock *gl, struct buffer_head *bh);
struct gfs_unlinked *gfs_trans_add_unlinked(struct gfs_sbd *sdp, unsigned int type,
					    struct gfs_inum *inum);
void gfs_trans_add_quota(struct gfs_sbd *sdp, int64_t change, uint32_t uid,
			 uint32_t gid);

#endif /* __TRANS_DOT_H__ */
