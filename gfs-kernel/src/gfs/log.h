#ifndef __LOG_DOT_H__
#define __LOG_DOT_H__

/**
 * gfs_log_lock - acquire the right to mess with the log manager
 * @sdp: the filesystem
 *
 */

static __inline__ void
gfs_log_lock(struct gfs_sbd *sdp)
{
	down_write(&sdp->sd_log_lock);
}

/**
 * gfs_log_unlock - release the right to mess with the log manager
 * @sdp: the filesystem
 *
 */

static __inline__ void
gfs_log_unlock(struct gfs_sbd *sdp)
{
	up_write(&sdp->sd_log_lock);
}

unsigned int gfs_struct2blk(struct gfs_sbd *sdp, unsigned int nstruct,
			    unsigned int ssize);
unsigned int gfs_blk2seg(struct gfs_sbd *sdp, unsigned int blocks);

int gfs_log_reserve(struct gfs_sbd *sdp, unsigned int segments, int jump_queue);
void gfs_log_release(struct gfs_sbd *sdp, unsigned int segments);

void gfs_ail_start(struct gfs_sbd *sdp, int flags);
int gfs_ail_empty(struct gfs_sbd *sdp);

void gfs_log_commit(struct gfs_sbd *sdp, struct gfs_trans *trans);
void gfs_log_flush(struct gfs_sbd *sdp);
void gfs_log_flush_glock(struct gfs_glock *gl);

void gfs_log_shutdown(struct gfs_sbd *sdp);

void gfs_log_dump(struct gfs_sbd *sdp, int force);

/*  Internal crap used the log operations  */

/**
 * gfs_log_is_header - Discover if block is on journal header
 * @sdp: The GFS superblock
 * @block: The block number
 *
 * Returns: TRUE if the block is on a journal segment boundary, FALSE otherwise
 */

static __inline__ int
gfs_log_is_header(struct gfs_sbd *sdp, uint64_t block)
{
	return !do_mod(block, sdp->sd_sb.sb_seg_size);
}

struct gfs_log_buf *gfs_log_get_buf(struct gfs_sbd *sdp, struct gfs_trans *tr);
void gfs_log_fake_buf(struct gfs_sbd *sdp, struct gfs_trans *tr, char *data,
		      struct buffer_head *unlock);

#endif /* __LOG_DOT_H__ */
