/*
 * Formats of Lock Value Blocks (LVBs) for various types of locks.
 * These 32-bit data chunks can be shared quickly between nodes
 *   via the inter-node lock manager (via LAN instead of on-disk).
 */

#ifndef __LVB_DOT_H__
#define __LVB_DOT_H__

#define GFS_MIN_LVB_SIZE (32)

/*
 * Resource Group block allocation statistics
 * Each resource group lock contains one of these in its LVB.
 * Used for sharing approximate current statistics for statfs.
 * Not used for actual block allocation.
 */
struct gfs_rgrp_lvb {
	uint32_t rb_magic;      /* GFS_MAGIC sanity check value */
	uint32_t rb_free;       /* # free data blocks */
	uint32_t rb_useddi;     /* # used dinode blocks */
	uint32_t rb_freedi;     /* # free dinode blocks */
	uint32_t rb_usedmeta;   /* # used metadata blocks */
	uint32_t rb_freemeta;   /* # free metadata blocks */
};

/*
 * Quota
 * Each quota lock contains one of these in its LVB.
 * Keeps track of block allocation limits and current block allocation
 *   for either a cluster-wide user or a cluster-wide group.
 */
struct gfs_quota_lvb {
	uint32_t qb_magic;      /* GFS_MAGIC sanity check value */
	uint32_t qb_pad;
	uint64_t qb_limit;      /* Hard limit of # blocks to alloc */
	uint64_t qb_warn;       /* Warn user when alloc is above this # */
	int64_t qb_value;       /* Current # blocks allocated */
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
