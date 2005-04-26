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

#ifndef __DIO_DOT_H__
#define __DIO_DOT_H__

void gfs2_ail1_start_one(struct gfs2_sbd *sdp, struct gfs2_ail *ai);
int gfs2_ail1_empty_one(struct gfs2_sbd *sdp, struct gfs2_ail *ai, int flags);
void gfs2_ail2_empty_one(struct gfs2_sbd *sdp, struct gfs2_ail *ai);
void gfs2_ail_empty_gl(struct gfs2_glock *gl);

/*  Asynchronous I/O Routines  */

struct buffer_head *gfs2_dgetblk(struct gfs2_glock *gl, uint64_t blkno);
int gfs2_dread(struct gfs2_glock *gl, uint64_t blkno,
	      int flags, struct buffer_head **bhp);

void gfs2_prep_new_buffer(struct buffer_head *bh);
int gfs2_dreread(struct gfs2_sbd *sdp, struct buffer_head *bh, int flags);
int gfs2_dwrite(struct gfs2_sbd *sdp, struct buffer_head *bh, int flags);

void gfs2_attach_bufdata(struct gfs2_glock *gl, struct buffer_head *bh);
void gfs2_dpin(struct gfs2_sbd *sdp, struct buffer_head *bh);
void gfs2_dunpin(struct gfs2_sbd *sdp, struct buffer_head *bh,
		struct gfs2_ail *ai);

void gfs2_logbh_init(struct gfs2_sbd *sdp, struct buffer_head *bh, uint64_t blkno,
		    char *data);
void gfs2_logbh_uninit(struct gfs2_sbd *sdp, struct buffer_head *bh);
void gfs2_logbh_start(struct gfs2_sbd *sdp, struct buffer_head *bh);
int gfs2_logbh_wait(struct gfs2_sbd *sdp, struct buffer_head *bh);

void gfs2_buf_wipe(struct gfs2_inode *ip, uint64_t bstart, uint32_t blen);

void gfs2_sync_meta(struct gfs2_sbd *sdp);

/*  Buffer Caching routines  */

int gfs2_get_meta_buffer(struct gfs2_inode *ip, int height, uint64_t num, int new,
			struct buffer_head **bhp);
int gfs2_get_data_buffer(struct gfs2_inode *ip, uint64_t block, int new,
			struct buffer_head **bhp);
void gfs2_start_ra(struct gfs2_glock *gl, uint64_t dblock, uint32_t extlen);

static __inline__ int
gfs2_get_inode_buffer(struct gfs2_inode *ip, struct buffer_head **bhp)
{
	return gfs2_get_meta_buffer(ip, 0, ip->i_num.no_addr, FALSE, bhp);
}

struct inode *gfs2_aspace_get(struct gfs2_sbd *sdp);
void gfs2_aspace_put(struct inode *aspace);

void gfs2_inval_buf(struct gfs2_glock *gl);
void gfs2_sync_buf(struct gfs2_glock *gl, int flags);

void gfs2_flush_meta_cache(struct gfs2_inode *ip);

/*  Buffer Content Functions  */

/**
 * gfs2_buffer_clear - Zeros out a buffer
 * @ip: The GFS2 inode
 * @bh: The buffer to zero
 *
 */

static __inline__ void
gfs2_buffer_clear(struct buffer_head *bh)
{
	memset(bh->b_data, 0, bh->b_size);
}

/**
 * gfs2_buffer_clear_tail - Clear buffer beyond the dinode
 * @bh: The buffer containing the on-disk inode
 * @head: the size of the head of the buffer
 *
 * Clears the remaining part of an on-disk inode that is not a dinode.
 * i.e. The data part of a stuffed inode, or the top level of metadata
 * of a non-stuffed inode.
 */

static __inline__ void
gfs2_buffer_clear_tail(struct buffer_head *bh, int head)
{
	memset(bh->b_data + head, 0, bh->b_size - head);
}

/**
 * gfs2_buffer_clear_ends - Zero out any bits of a buffer which are not being written
 * @bh: The buffer
 * @offset: Offset in buffer where write starts
 * @amount: Amount of data being written
 * @journaled: TRUE if this is a journaled buffer
 *
 */

static __inline__ void
gfs2_buffer_clear_ends(struct buffer_head *bh, int offset, int amount,
		      int journaled)
{
	int z_off1 = (journaled) ? sizeof(struct gfs2_meta_header) : 0;
	int z_len1 = offset - z_off1;
	int z_off2 = offset + amount;
	int z_len2 = (bh)->b_size - z_off2;

	if (z_len1)
		memset(bh->b_data + z_off1, 0, z_len1);

	if (z_len2)
		memset(bh->b_data + z_off2, 0, z_len2);
}

/**
 * gfs2_buffer_copy_tail - copies the tail of one buffer to another
 * @to_bh: the buffer to copy to
 * @to_head: the size of the head of to_bh
 * @from_bh: the buffer to copy from
 * @from_head: the size of the head of from_bh
 *
 * from_head is guaranteed to bigger than to_head 
 */

static __inline__ void
gfs2_buffer_copy_tail(struct buffer_head *to_bh, int to_head,
		     struct buffer_head *from_bh, int from_head)
{
	memcpy(to_bh->b_data + to_head,
	       from_bh->b_data + from_head,
	       from_bh->b_size - from_head);
	memset(to_bh->b_data + to_bh->b_size + to_head - from_head,
	       0,
	       from_head - to_head);
}

#endif /* __DIO_DOT_H__ */
