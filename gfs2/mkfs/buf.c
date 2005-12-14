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

#include "gfs2_mkfs.h"

#define do_lseek(sdp, off) \
do { \
	if (lseek((sdp)->device_fd, (off), SEEK_SET) != (off)) \
		die("bad seek: %s on line %d of file %s\n", \
		    strerror(errno), __LINE__, __FILE__); \
} while (0)

#define do_read(sdp, buf, len) \
do { \
	if (read((sdp)->device_fd, (buf), (len)) != (len)) \
		die("bad read: %s on line %d of file %s\n", \
		    strerror(errno), __LINE__, __FILE__); \
} while (0)

#define do_write(sdp, buf, len) \
do { \
	if (write((sdp)->device_fd, (buf), (len)) != (len)) \
		die("bad write: %s on line %d of file %s\n", \
		    strerror(errno), __LINE__, __FILE__); \
} while (0)

static __inline__ osi_list_t *
blkno2head(struct gfs2_sbd *sdp, uint64_t blkno)
{
	return sdp->buf_hash +
		(gfs2_disk_hash((char *)&blkno, sizeof(uint64_t)) & BUF_HASH_MASK);
}

static void
write_buffer(struct gfs2_sbd *sdp, struct buffer_head *bh)
{
	if (!bh->b_uninit) {
		struct gfs2_meta_header mh;
		gfs2_meta_header_in(&mh, bh->b_data);
		if (mh.mh_magic != GFS2_MAGIC ||
		    mh.mh_blkno != bh->b_blocknr)
			fprintf(stderr, "gfs2_mkfs: uninitialized block %"PRIu64"\n",
				bh->b_blocknr);
	}

	osi_list_del(&bh->b_list);
	osi_list_del(&bh->b_hash);
	sdp->num_bufs--;

	do_lseek(sdp, bh->b_blocknr * sdp->bsize);
	do_write(sdp, bh->b_data, sdp->bsize);

	free(bh);

	sdp->writes++;
}

static void
add_buffer(struct gfs2_sbd *sdp, struct buffer_head *bh)
{
	osi_list_t *head = blkno2head(sdp, bh->b_blocknr);

	osi_list_add(&bh->b_list, &sdp->buf_list);
	osi_list_add(&bh->b_hash, head);
	sdp->num_bufs++;

	while (sdp->num_bufs * sdp->bsize > 128 << 20) {
		bh = osi_list_entry(sdp->buf_list.prev, struct buffer_head, b_list);
		if (bh->b_count) {
			osi_list_del(&bh->b_list);
			osi_list_add(&bh->b_list, &sdp->buf_list);
			continue;
		}
		write_buffer(sdp, bh);
		sdp->spills++;
	} 
}

struct buffer_head *
bfind(struct gfs2_sbd *sdp, uint64_t num)
{
	osi_list_t *head = blkno2head(sdp, num);
	osi_list_t *tmp;
	struct buffer_head *bh;

	for (tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		bh = osi_list_entry(tmp, struct buffer_head, b_hash);
		if (bh->b_blocknr == num) {
			osi_list_del(&bh->b_list);
			osi_list_add(&bh->b_list, &sdp->buf_list);
			osi_list_del(&bh->b_hash);
			osi_list_add(&bh->b_hash, head);
			bh->b_count++;
			return bh;
		}
	}

	return NULL;
}

struct buffer_head *
bget(struct gfs2_sbd *sdp, uint64_t num)
{
	struct buffer_head *bh;

	bh = bfind(sdp, num);
	if (bh)
		return bh;

	zalloc(bh, sizeof(struct buffer_head) + sdp->bsize);

	bh->b_count = 1;
	bh->b_blocknr = num;
	bh->b_data = (char *)bh + sizeof(struct buffer_head);
	bh->b_size = sdp->bsize;

	add_buffer(sdp, bh);

	return bh;
}

struct buffer_head *
bread(struct gfs2_sbd *sdp, uint64_t num)
{
	struct buffer_head *bh;

	bh = bfind(sdp, num);
	if (bh)
		return bh;

	zalloc(bh, sizeof(struct buffer_head) + sdp->bsize);

	bh->b_count = 1;
	bh->b_blocknr = num;
	bh->b_data = (char *)bh + sizeof(struct buffer_head);
	bh->b_size = sdp->bsize;

	do_lseek(sdp, num * sdp->bsize);
	do_read(sdp, bh->b_data, sdp->bsize);

	add_buffer(sdp, bh);

	return bh;
}

struct buffer_head *
bhold(struct buffer_head *bh)
{
	if (!bh->b_count)
		die("buffer hold error\n");
	bh->b_count++;
	return bh;
}

void
brelse(struct buffer_head *bh)
{
	if (!bh->b_count)
		die("buffer count underflow\n");
	bh->b_count--;
}

void
bsync(struct gfs2_sbd *sdp)
{
	struct buffer_head *bh;

	while (!osi_list_empty(&sdp->buf_list)) {
		bh = osi_list_entry(sdp->buf_list.prev, struct buffer_head, b_list);
		if (bh->b_count)
			die("buffer still held: %"PRIu64"\n",
			    bh->b_blocknr);
		write_buffer(sdp, bh);
	}
}

