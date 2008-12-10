#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <linux/types.h>

#include "libgfs2.h"

static __inline__ osi_list_t *
blkno2head(struct buf_list *bl, uint64_t blkno)
{
	return bl->buf_hash +
		(gfs2_disk_hash((char *)&blkno, sizeof(uint64_t)) & BUF_HASH_MASK);
}

static void write_buffer(struct buf_list *bl, struct gfs2_buffer_head *bh)
{
	struct gfs2_sbd *sdp = bl->sbp;

	osi_list_del(&bh->b_list);
	osi_list_del(&bh->b_hash);
	bl->num_bufs--;
	if (bh->b_changed) {
		do_lseek(sdp->device_fd, bh->b_blocknr * sdp->bsize);
		do_write(sdp->device_fd, bh->b_data, sdp->bsize);
		sdp->writes++;
	}
	free(bh);
}

void init_buf_list(struct gfs2_sbd *sdp, struct buf_list *bl, uint32_t limit)
{
	int i;

	bl->num_bufs = 0;
	bl->spills = 0;
	bl->limit = limit;
	bl->sbp = sdp;
	osi_list_init(&bl->list);
	for(i = 0; i < BUF_HASH_SIZE; i++)
		osi_list_init(&bl->buf_hash[i]);
}

static void
add_buffer(struct buf_list *bl, struct gfs2_buffer_head *bh)
{
	osi_list_t *head = blkno2head(bl, bh->b_blocknr);

	osi_list_add(&bh->b_list, &bl->list);
	osi_list_add(&bh->b_hash, head);
	bl->num_bufs++;

	if (bl->num_bufs * bl->sbp->bsize > bl->limit) {
		int found = 0;
		osi_list_t *tmp, *x;

		for (tmp = bl->list.prev, x = tmp->prev; tmp != &bl->list;
		     tmp = x, x = x->prev) {
			bh = osi_list_entry(tmp, struct gfs2_buffer_head,
					    b_list);
			if (!bh->b_count) {
				write_buffer(bl, bh);
				found++;
				if (found >= 10)
					break;
			}
		}
		bl->spills++;
	}
}

struct gfs2_buffer_head *bfind(struct buf_list *bl, uint64_t num)
{
	osi_list_t *head = blkno2head(bl, num);
	osi_list_t *tmp;
	struct gfs2_buffer_head *bh;

	for (tmp = head->next; tmp != head; tmp = tmp->next) {
		bh = osi_list_entry(tmp, struct gfs2_buffer_head, b_hash);
		if (bh->b_blocknr == num) {
			osi_list_del(&bh->b_list);
			osi_list_add(&bh->b_list, &bl->list);
			osi_list_del(&bh->b_hash);
			osi_list_add(&bh->b_hash, head);
			bh->b_count++;
			return bh;
		}
	}

	return NULL;
}

struct gfs2_buffer_head *bget_generic(struct buf_list *bl, uint64_t num,
				      int find_existing, int read_disk)
{
	struct gfs2_buffer_head *bh;
	struct gfs2_sbd *sdp = bl->sbp;

	if (find_existing) {
		bh = bfind(bl, num);
		if (bh)
			return bh;
	}
	zalloc(bh, sizeof(struct gfs2_buffer_head) + sdp->bsize);

	bh->b_count = 1;
	bh->b_blocknr = num;
	bh->b_data = (char *)bh + sizeof(struct gfs2_buffer_head);
	if (read_disk) {
		do_lseek(sdp->device_fd, num * sdp->bsize);
		do_read(sdp->device_fd, bh->b_data, sdp->bsize);
	}
	add_buffer(bl, bh);
	bh->b_changed = FALSE;

	return bh;
}

struct gfs2_buffer_head *bget(struct buf_list *bl, uint64_t num)
{
	return bget_generic(bl, num, TRUE, FALSE);
}

struct gfs2_buffer_head *bread(struct buf_list *bl, uint64_t num)
{
	return bget_generic(bl, num, TRUE, TRUE);
}

struct gfs2_buffer_head *bget_zero(struct buf_list *bl, uint64_t num)
{
	return bget_generic(bl, num, FALSE, FALSE);
}

struct gfs2_buffer_head *bhold(struct gfs2_buffer_head *bh)
{
	if (!bh->b_count)
		die("buffer hold error for block %" PRIu64 " (0x%" PRIx64")\n",
			bh->b_blocknr, bh->b_blocknr);
	bh->b_count++;
	return bh;
}

void brelse(struct gfs2_buffer_head *bh, enum update_flags updated)
{
    /* We can't just say b_changed = updated because we don't want to     */
	/* set it FALSE if it's TRUE until we write the changed data to disk. */
	if (updated)
		bh->b_changed = TRUE;
	if (!bh->b_count)
		die("buffer count underflow for block %" PRIu64 " (0x%" PRIx64")\n",
			bh->b_blocknr, bh->b_blocknr);
	bh->b_count--;
}

void bsync(struct buf_list *bl)
{
	struct gfs2_buffer_head *bh;

	while (!osi_list_empty(&bl->list)) {
		bh = osi_list_entry(bl->list.prev, struct gfs2_buffer_head,
							b_list);
		if (bh->b_count)
			die("buffer still held for block: %" PRIu64 " (0x%" PRIx64")\n",
				bh->b_blocknr, bh->b_blocknr);
		write_buffer(bl, bh);
	}
}

/* commit buffers to disk but do not discard */
void bcommit(struct buf_list *bl)
{
	osi_list_t *tmp, *x;
	struct gfs2_buffer_head *bh;
	struct gfs2_sbd *sdp = bl->sbp;

	osi_list_foreach_safe(tmp, &bl->list, x) {
		bh = osi_list_entry(tmp, struct gfs2_buffer_head, b_list);
		if (!bh->b_count)             /* if not reserved for later */
			write_buffer(bl, bh);/* write the data & free memory */
		else if (bh->b_changed) {     /* if buffer has changed */
			do_lseek(sdp->device_fd, bh->b_blocknr * sdp->bsize);
			do_write(sdp->device_fd, bh->b_data, sdp->bsize);
			bh->b_changed = FALSE;    /* no longer changed */
		}
	}
	fsync(sdp->device_fd);
}

/* Check for unreleased buffers */
void bcheck(struct buf_list *bl)
{
	osi_list_t *tmp;
	struct gfs2_buffer_head *bh;

	osi_list_foreach(tmp, &bl->list) {
		bh = osi_list_entry(tmp, struct gfs2_buffer_head, b_list);
		if (bh->b_count)
			die("buffer still held: %"PRIu64"\n", bh->b_blocknr);
	}
}
