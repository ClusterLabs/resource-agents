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

/**
 * how_many_rgrps - figure out how many RG to put in a subdevice
 * @w: the command line
 * @dev: the device
 *
 * Returns: the number of RGs
 */

uint64_t
how_many_rgrps(struct gfs2_sbd *sdp, struct device *dev, int rgsize_specified)
{
	uint64_t nrgrp;

	while (TRUE) {
		nrgrp = DIV_RU(dev->length, (sdp->rgsize << 20) / sdp->bsize);

		if (rgsize_specified || /* If user specified an rg size or */
			nrgrp <= GFS2_EXCESSIVE_RGS || /* not an excessive # of rgs or  */
			sdp->rgsize >= 2048)     /* we've reached the max rg size */
			break;

		sdp->rgsize += GFS2_DEFAULT_RGSIZE; /* Try again w/bigger rgs */
	}

	if (sdp->debug)
		printf("  rg sz = %"PRIu32"\n  nrgrp = %"PRIu64"\n", sdp->rgsize,
			   nrgrp);

	return nrgrp;
}

/**
 * compute_rgrp_layout - figure out where the RG in a FS are
 * @w: the command line
 *
 * Returns: a list of rgrp_list_t structures
 */

void
compute_rgrp_layout(struct gfs2_sbd *sdp, int rgsize_specified)
{
	struct device *dev;
	struct rgrp_list *rl, *rlast = NULL, *rlast2 = NULL;
	osi_list_t *tmp, *head = &sdp->rglist;
	unsigned int rgrp = 0, nrgrp;
	uint64_t rglength;

	sdp->new_rgrps = 0;
	dev = &sdp->device;

	/* Reserve space for the superblock */
	dev->start += sdp->sb_addr + 1;

	/* If this is a new file system, compute the length and number */
	/* of rgs based on the size of the device.                     */
	/* If we have existing RGs (i.e. gfs2_grow) find the last one. */
	if (osi_list_empty(&sdp->rglist)) {
		dev->length -= sdp->sb_addr + 1;
		nrgrp = how_many_rgrps(sdp, dev, rgsize_specified);
		rglength = dev->length / nrgrp;
		sdp->new_rgrps = nrgrp;
	} else {
		uint64_t old_length, new_chunk;

		log_info("Existing resource groups:\n");
		rgsize_specified = TRUE; /* consistently use existing size */
		for (rgrp = 0, tmp = head->next; tmp != head;
		     tmp = tmp->next, rgrp++) {
			rl = osi_list_entry(tmp, struct rgrp_list, list);
			log_info("%d: start: %" PRIu64 " (0x%"
				 PRIx64 "), length = %"PRIu64" (0x%"
				 PRIx64 ")\n", rgrp + 1, rl->start, rl->start,
				 rl->length, rl->length);
			rlast2 = rlast;
			rlast = rl;
		}
		rlast->start = rlast->ri.ri_addr;
		rglength = rlast->ri.ri_addr - rlast2->ri.ri_addr;
		rlast->length = rglength;
		old_length = rlast->ri.ri_addr + rglength;
		new_chunk = dev->length - old_length;
		sdp->new_rgrps = new_chunk / rglength;
		nrgrp = rgrp + sdp->new_rgrps;
	}

	log_info("\nNew resource groups:\n");
	for (; rgrp < nrgrp; rgrp++) {
		zalloc(rl, sizeof(struct rgrp_list));

		if (rgrp) {
			rl->start = rlast->start + rlast->length;
			rl->length = rglength;
		} else {
			rl->start = dev->start;
			rl->length = dev->length -
				(nrgrp - 1) * (dev->length / nrgrp);
		}
		rl->rgf_flags = dev->rgf_flags;

		log_info("%d: start: %" PRIu64 " (0x%"
			 PRIx64 "), length = %"PRIu64" (0x%"
			 PRIx64 ")\n", rgrp + 1, rl->start, rl->start,
			 rl->length, rl->length);
		osi_list_add_prev(&rl->list, head);
		rlast = rl;
	}

	sdp->rgrps = nrgrp;

	if (sdp->debug) {
		log_info("\n");

		for (tmp = head->next; tmp != head; tmp = tmp->next) {
			rl = osi_list_entry(tmp, struct rgrp_list, list);
			log_info("rg_o = %llu, rg_l = %llu\n",
				 rl->start, rl->length);
		}
	}
}

/**
 * rgblocks2bitblocks -
 * @bsize:
 * @rgblocks:
 * @bitblocks:
 *
 * Given a number of blocks in a RG, figure out the number of blocks
 * needed for bitmaps.
 *
 */

void
rgblocks2bitblocks(unsigned int bsize, uint32_t *rgblocks, uint32_t *bitblocks)
{
	unsigned int bitbytes_provided, last = 0;
	unsigned int bitbytes_needed;

	*bitblocks = 1;
	bitbytes_provided = bsize - sizeof(struct gfs2_rgrp);

	for (;;) {
	        bitbytes_needed = (*rgblocks - *bitblocks) / GFS2_NBBY;

		if (bitbytes_provided >= bitbytes_needed) {
			if (last >= bitbytes_needed)
				(*bitblocks)--;
			break;
		}

		last = bitbytes_provided;
		(*bitblocks)++;
		bitbytes_provided += bsize - sizeof(struct gfs2_meta_header);
	}

	*rgblocks = bitbytes_needed * GFS2_NBBY;
}

/**
 * build_rgrps - write a bunch of resource groups to disk.
 * If fd > 0, write the data to the given file handle.
 * Otherwise, use gfs2 buffering in buf.c.
 */
void build_rgrps(struct gfs2_sbd *sdp, int write)
{
	osi_list_t *tmp, *head;
	struct rgrp_list *rl;
	uint32_t rgblocks, bitblocks;
	struct gfs2_rindex *ri;
	struct gfs2_rgrp *rg;
	struct gfs2_meta_header mh;
	unsigned int x;
	struct gfs2_buffer_head *bh;

	mh.mh_magic = GFS2_MAGIC;
	mh.mh_type = GFS2_METATYPE_RB;
	mh.mh_format = GFS2_FORMAT_RB;

	for (head = &sdp->rglist, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		rl = osi_list_entry(tmp, struct rgrp_list, list);
		ri = &rl->ri;
		rg = &rl->rg;

		rgblocks = rl->length;
		rgblocks2bitblocks(sdp->bsize, &rgblocks, &bitblocks);

		ri->ri_addr = rl->start;
		ri->ri_length = bitblocks;
		ri->ri_data0 = rl->start + bitblocks;
		ri->ri_data = rgblocks;
		ri->ri_bitbytes = rgblocks / GFS2_NBBY;

		rg->rg_header.mh_magic = GFS2_MAGIC;
		rg->rg_header.mh_type = GFS2_METATYPE_RG;
		rg->rg_header.mh_format = GFS2_FORMAT_RG;
		rg->rg_flags = rl->rgf_flags;
		rg->rg_free = rgblocks;

		if (write) {
			for (x = 0; x < bitblocks; x++) {
				bh = bget(&sdp->nvbuf_list, rl->start + x);
				if (x)
					gfs2_meta_header_out(&mh, bh->b_data);
				else
					gfs2_rgrp_out(rg, bh->b_data);
				brelse(bh, updated);
			}
		}

		if (sdp->debug) {
			printf("\n");
			gfs2_rindex_print(ri);
		}

		sdp->blks_total += rgblocks;
		sdp->fssize = ri->ri_data0 + ri->ri_data;
	}
}
