#ifndef __DIO_DOT_H__
#define __DIO_DOT_H__

void gfs_ail_start_trans(struct gfs_sbd *sdp, struct gfs_trans *tr);
int gfs_ail_empty_trans(struct gfs_sbd *sdp, struct gfs_trans *tr);

/*  Asynchronous I/O Routines  */

struct buffer_head *gfs_dgetblk(struct gfs_glock *gl, uint64_t blkno);
int gfs_dread(struct gfs_glock *gl, uint64_t blkno,
	      int flags, struct buffer_head **bhp);

void gfs_prep_new_buffer(struct buffer_head *bh);
int gfs_dreread(struct gfs_sbd *sdp, struct buffer_head *bh, int flags);
int gfs_dwrite(struct gfs_sbd *sdp, struct buffer_head *bh, int flags);

void gfs_attach_bufdata(struct buffer_head *bh, struct gfs_glock *gl);
int gfs_is_pinned(struct gfs_sbd *sdp, struct buffer_head *bh);
void gfs_dpin(struct gfs_sbd *sdp, struct buffer_head *bh);
void gfs_dunpin(struct gfs_sbd *sdp, struct buffer_head *bh,
		struct gfs_trans *tr);

static __inline__
void gfs_lock_buffer(struct buffer_head *bh)
{
	struct gfs_bufdata *bd = get_v2bd(bh);
	down(&bd->bd_lock);
}
static __inline__
int gfs_trylock_buffer(struct buffer_head *bh)
{
	struct gfs_bufdata *bd = get_v2bd(bh);
	return down_trylock(&bd->bd_lock);
}
static __inline__
void gfs_unlock_buffer(struct buffer_head *bh)
{
	struct gfs_bufdata *bd = get_v2bd(bh);
	up(&bd->bd_lock);
}

void gfs_logbh_init(struct gfs_sbd *sdp, struct buffer_head *bh, uint64_t blkno,
		    char *data);
void gfs_logbh_uninit(struct gfs_sbd *sdp, struct buffer_head *bh);
void gfs_logbh_start(struct gfs_sbd *sdp, struct buffer_head *bh);
int gfs_logbh_wait(struct gfs_sbd *sdp, struct buffer_head *bh);

int gfs_replay_buf(struct gfs_glock *gl, struct buffer_head *bh);
void gfs_replay_check(struct gfs_sbd *sdp);
void gfs_replay_wait(struct gfs_sbd *sdp);

void gfs_wipe_buffers(struct gfs_inode *ip, struct gfs_rgrpd *rgd,
		      uint64_t bstart, uint32_t blen);

void gfs_sync_meta(struct gfs_sbd *sdp);

/*  Buffer Caching routines  */

int gfs_get_meta_buffer(struct gfs_inode *ip, int height, uint64_t num, int new,
			struct buffer_head **bhp);
int gfs_get_data_buffer(struct gfs_inode *ip, uint64_t block, int new,
			struct buffer_head **bhp);
void gfs_start_ra(struct gfs_glock *gl, uint64_t dblock, uint32_t extlen);

static __inline__ int
gfs_get_inode_buffer(struct gfs_inode *ip, struct buffer_head **bhp)
{
	return gfs_get_meta_buffer(ip, 0, ip->i_num.no_addr, FALSE, bhp);
}

struct inode *gfs_aspace_get(struct gfs_sbd *sdp);
void gfs_aspace_put(struct inode *aspace);

void gfs_inval_buf(struct gfs_glock *gl);
void gfs_sync_buf(struct gfs_glock *gl, int flags);

void gfs_flush_meta_cache(struct gfs_inode *ip);

/*  Buffer Content Functions  */

/**
 * gfs_buffer_clear - Zeros out a buffer
 * @ip: The GFS inode
 * @bh: The buffer to zero
 *
 */

static __inline__ void
gfs_buffer_clear(struct buffer_head *bh)
{
	memset(bh->b_data, 0, bh->b_size);
}

/**
 * gfs_buffer_clear_tail - Clear buffer beyond the dinode
 * @bh: The buffer containing the on-disk inode
 * @head: the size of the head of the buffer
 *
 * Clears the remaining part of an on-disk inode that is not a dinode.
 * i.e. The data part of a stuffed inode, or the top level of metadata
 * of a non-stuffed inode.
 */

static __inline__ void
gfs_buffer_clear_tail(struct buffer_head *bh, int head)
{
	memset(bh->b_data + head, 0, bh->b_size - head);
}

/**
 * gfs_buffer_clear_ends - Zero out any bits of a buffer which are not being written
 * @bh: The buffer
 * @offset: Offset in buffer where write starts
 * @amount: Amount of data being written
 * @journaled: TRUE if this is a journaled buffer
 *
 */

static __inline__ void
gfs_buffer_clear_ends(struct buffer_head *bh, int offset, int amount,
		      int journaled)
{
	int z_off1 = (journaled) ? sizeof(struct gfs_meta_header) : 0;
	int z_len1 = offset - z_off1;
	int z_off2 = offset + amount;
	int z_len2 = (bh)->b_size - z_off2;

	if (z_len1)
		memset(bh->b_data + z_off1, 0, z_len1);

	if (z_len2)
		memset(bh->b_data + z_off2, 0, z_len2);
}

/**
 * gfs_buffer_copy_tail - copies the tail of one buffer to another
 * @to_bh: the buffer to copy to
 * @to_head: the size of the head of to_bh
 * @from_bh: the buffer to copy from
 * @from_head: the size of the head of from_bh
 *
 * from_head is guaranteed to bigger than to_head 
 */

static __inline__ void
gfs_buffer_copy_tail(struct buffer_head *to_bh, int to_head,
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
