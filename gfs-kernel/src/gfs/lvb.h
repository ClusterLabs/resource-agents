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

#ifndef __LVB_DOT_H__
#define __LVB_DOT_H__

#define GFS_MIN_LVB_SIZE (32)

struct gfs_rgrp_lvb {
	uint32_t rb_magic;
	uint32_t rb_free;
	uint32_t rb_useddi;
	uint32_t rb_freedi;
	uint32_t rb_usedmeta;
	uint32_t rb_freemeta;
};

struct gfs_quota_lvb {
	uint32_t qb_magic;
	uint32_t qb_pad;
	uint64_t qb_limit;
	uint64_t qb_warn;
	int64_t qb_value;
};

/*  Translation functions  */

void gfs_rgrp_lvb_in(struct gfs_rgrp_lvb *rb, char *lvb);
void gfs_rgrp_lvb_out(struct gfs_rgrp_lvb *rb, char *lvb);
void gfs_quota_lvb_in(struct gfs_quota_lvb *qb, char *lvb);
void gfs_quota_lvb_out(struct gfs_quota_lvb *qb, char *lvb);

/*  Printing functions  */

void gfs_rgrp_lvb_print(struct gfs_rgrp_lvb *rb);
void gfs_quota_lvb_print(struct gfs_quota_lvb *qb);

#endif /* __LVB_DOT_H__ */
