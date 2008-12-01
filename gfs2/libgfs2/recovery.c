/*
 * NOTE:
 *
 * This code was pilfered from the gfs2 kernel and adapted to userland.
 * If you change this part, you should evaluate whether the upstream kernel
 * version of recovery.c should be changed as well.  Likewise, if the
 * upstream version changes, this part should be kept in sync.
 * 
 */

#include <errno.h>
#include <string.h>
#include "libgfs2.h"

void gfs2_replay_incr_blk(struct gfs2_inode *ip, unsigned int *blk)
{
	uint32_t jd_blocks = ip->i_di.di_size / ip->i_sbd->sd_sb.sb_bsize;

        if (++*blk == jd_blocks)
                *blk = 0;
}

int gfs2_replay_read_block(struct gfs2_inode *ip, unsigned int blk,
			   struct gfs2_buffer_head **bh)
{
	int new = 0;
	uint64_t dblock;
	uint32_t extlen;

	block_map(ip, blk, &new, &dblock, &extlen, FALSE, not_updated);
	if (!dblock)
		return -EIO;

	*bh = bread(&ip->i_sbd->buf_list, dblock);
	return 0;
}

/**
 * get_log_header - read the log header for a given segment
 * @ip: the journal incore inode
 * @blk: the block to look at
 * @lh: the log header to return
 *
 * Read the log header for a given segement in a given journal.  Do a few
 * sanity checks on it.
 *
 * Returns: 0 on success,
 *          1 if the header was invalid or incomplete,
 *          errno on error
 */

int get_log_header(struct gfs2_inode *ip, unsigned int blk,
		   struct gfs2_log_header *head)
{
	struct gfs2_buffer_head *bh;
	struct gfs2_log_header lh, *tmp;
	uint32_t hash, saved_hash;
	int error;

	error = gfs2_replay_read_block(ip, blk, &bh);
	if (error)
		return error;

	tmp = (struct gfs2_log_header *)bh->b_data;
	saved_hash = tmp->lh_hash;
	tmp->lh_hash = 0;
	hash = gfs2_disk_hash(bh->b_data, sizeof(struct gfs2_log_header));
	tmp->lh_hash = saved_hash;
	gfs2_log_header_in(&lh, bh->b_data);
	brelse(bh, not_updated);

	if (error || lh.lh_blkno != blk || lh.lh_hash != hash)
		return 1;

	*head = lh;

	return 0;
}

/**
 * find_good_lh - find a good log header
 * @ip: the journal incore inode
 * @blk: the segment to start searching from
 * @lh: the log header to fill in
 * @forward: if true search forward in the log, else search backward
 *
 * Call get_log_header() to get a log header for a segment, but if the
 * segment is bad, either scan forward or backward until we find a good one.
 *
 * Returns: errno
 */

int find_good_lh(struct gfs2_inode *ip, unsigned int *blk,
		 struct gfs2_log_header *head)
{
	unsigned int orig_blk = *blk;
	int error;
	uint32_t jd_blocks = ip->i_di.di_size / ip->i_sbd->sd_sb.sb_bsize;

	for (;;) {
		error = get_log_header(ip, *blk, head);
		if (error <= 0)
			return error;

		if (++*blk == jd_blocks)
			*blk = 0;

		if (*blk == orig_blk)
			return -EIO;
	}
}

/**
 * jhead_scan - make sure we've found the head of the log
 * @jd: the journal
 * @head: this is filled in with the log descriptor of the head
 *
 * At this point, seg and lh should be either the head of the log or just
 * before.  Scan forward until we find the head.
 *
 * Returns: errno
 */

int jhead_scan(struct gfs2_inode *ip, struct gfs2_log_header *head)
{
	unsigned int blk = head->lh_blkno;
	uint32_t jd_blocks = ip->i_di.di_size / ip->i_sbd->sd_sb.sb_bsize;
	struct gfs2_log_header lh;
	int error;

	for (;;) {
		if (++blk == jd_blocks)
			blk = 0;

		error = get_log_header(ip, blk, &lh);
		if (error < 0)
			return error;
		if (error == 1)
			continue;

		if (lh.lh_sequence == head->lh_sequence)
			return -EIO;
		if (lh.lh_sequence < head->lh_sequence)
			break;

		*head = lh;
	}

	return 0;
}

/**
 * gfs2_find_jhead - find the head of a log
 * @jd: the journal
 * @head: the log descriptor for the head of the log is returned here
 *
 * Do a binary search of a journal and find the valid log entry with the
 * highest sequence number.  (i.e. the log head)
 *
 * Returns: errno
 */

int gfs2_find_jhead(struct gfs2_inode *ip, struct gfs2_log_header *head)
{
	struct gfs2_log_header lh_1, lh_m;
	uint32_t blk_1, blk_2, blk_m;
	uint32_t jd_blocks = ip->i_di.di_size / ip->i_sbd->sd_sb.sb_bsize;
	int error;

	blk_1 = 0;
	blk_2 = jd_blocks - 1;

	for (;;) {
		blk_m = (blk_1 + blk_2) / 2;

		error = find_good_lh(ip, &blk_1, &lh_1);
		if (error)
			return error;

		error = find_good_lh(ip, &blk_m, &lh_m);
		if (error)
			return error;

		if (blk_1 == blk_m || blk_m == blk_2)
			break;

		if (lh_1.lh_sequence <= lh_m.lh_sequence)
			blk_1 = blk_m;
		else
			blk_2 = blk_m;
	}

	error = jhead_scan(ip, &lh_1);
	if (error)
		return error;

	*head = lh_1;

	return error;
}

/**
 * clean_journal - mark a dirty journal as being clean
 * @sdp: the filesystem
 * @jd: the journal
 * @head: the head journal to start from
 *
 * Returns: errno
 */

int clean_journal(struct gfs2_inode *ip, struct gfs2_log_header *head)
{
	unsigned int lblock;
	struct gfs2_log_header *lh;
	uint32_t hash, extlen;
	struct gfs2_buffer_head *bh;
	int new = 0;
	uint64_t dblock;

	lblock = head->lh_blkno;
	gfs2_replay_incr_blk(ip, &lblock);
	block_map(ip, lblock, &new, &dblock, &extlen, 0, not_updated);
	if (!dblock)
		return -EIO;

	bh = bread(&ip->i_sbd->buf_list, dblock);
	memset(bh->b_data, 0, ip->i_sbd->bsize);

	lh = (struct gfs2_log_header *)bh->b_data;
	memset(lh, 0, sizeof(struct gfs2_log_header));
	lh->lh_header.mh_magic = cpu_to_be32(GFS2_MAGIC);
	lh->lh_header.mh_type = cpu_to_be32(GFS2_METATYPE_LH);
	lh->lh_header.mh_format = cpu_to_be32(GFS2_FORMAT_LH);
	lh->lh_sequence = cpu_to_be64(head->lh_sequence + 1);
	lh->lh_flags = cpu_to_be32(GFS2_LOG_HEAD_UNMOUNT);
	lh->lh_blkno = cpu_to_be32(lblock);
	hash = gfs2_disk_hash((const char *)lh, sizeof(struct gfs2_log_header));
	lh->lh_hash = cpu_to_be32(hash);

	brelse(bh, updated);

	return 0;
}

