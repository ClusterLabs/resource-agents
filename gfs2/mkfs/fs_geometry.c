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

#include "gfs2_mkfs.h"

/**
 * how_many_rgrps - figure out how many RG to put in a subdevice
 * @w: the command line
 * @sdev: the subdevice
 *
 * Returns: the number of RGs
 */

static uint64_t
how_many_rgrps(struct gfs2_sbd *sdp, struct subdevice *sdev)
{
	uint64_t nrgrp = DIV_RU(sdev->length,
				(sdp->rgsize << 20) / sdp->bsize);

	if (sdp->debug)
		printf("  nrgrp = %"PRIu64"\n", nrgrp);

	return nrgrp;
}

/**
 * compute_rgrp_layout - figure out where the RG in a FS are
 * @w: the command line
 *
 * Returns: a list of rgrp_list_t structures
 */

void
compute_rgrp_layout(struct gfs2_sbd *sdp, int new_fs)
{
	struct subdevice *sdev;
	struct rgrp_list *rl, *rlast = NULL;
	osi_list_t *tmp, *head = &sdp->rglist;
	uint64_t rgrp, nrgrp;
	unsigned int x;

	for (x = 0; x < sdp->device.nsubdev; x++) {
		sdev = sdp->device.subdev + x;

		/* If this is the first subdevice reserve space for the superblock */
		if (new_fs) {
			sdev->start += sdp->sb_addr + 1;
			sdev->length -= sdp->sb_addr + 1;
			new_fs = FALSE;
		}

		if (sdp->debug)
			printf("\nData Subdevice %u\n", x);

		nrgrp = how_many_rgrps(sdp, sdev);

		for (rgrp = 0; rgrp < nrgrp; rgrp++) {
			zalloc(rl, sizeof(struct rgrp_list));

			rl->subdevice = x;

			if (rgrp) {
				rl->start = rlast->start + rlast->length;
				rl->length = sdev->length / nrgrp;
			} else {
				rl->start = sdev->start;
				rl->length = sdev->length -
					(nrgrp - 1) * (sdev->length / nrgrp);
			}
			rl->rgf_flags = sdev->rgf_flags;

			osi_list_add_prev(&rl->list, head);

			rlast = rl;
		}

		sdp->rgrps += nrgrp;
		sdp->new_rgrps += nrgrp;
	}

	if (sdp->debug) {
		printf("\n");

		for (tmp = head->next; tmp != head; tmp = tmp->next) {
			rl = osi_list_entry(tmp, struct rgrp_list, list);
			printf("subdevice %u:  rg_o = %"PRIu64", rg_l = %"PRIu64"\n",
			       rl->subdevice,
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
 * Give a number of blocks in a RG, figure out the number of blocks
 * needed for bitmaps.
 *
 */

static void
rgblocks2bitblocks(unsigned int bsize,
		   uint32_t *rgblocks,
		   uint32_t *bitblocks)
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

void
build_rgrps(struct gfs2_sbd *sdp)
{
	osi_list_t *tmp, *head;
	struct rgrp_list *rl;
	uint32_t rgblocks, bitblocks;
	struct gfs2_rindex *ri;
	struct gfs2_rgrp *rg;
	struct gfs2_meta_header mh;
	unsigned int x;
	struct buffer_head *bh;

	mh.mh_magic = GFS2_MAGIC;
	mh.mh_type = GFS2_METATYPE_RB;
	mh.mh_format = GFS2_FORMAT_RB;
	mh.mh_pad = 0;

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
		rg->rg_header.mh_blkno = rl->start;
		rg->rg_header.mh_format = GFS2_FORMAT_RG;
		rg->rg_flags = rl->rgf_flags;
		rg->rg_free = rgblocks;

		if (!sdp->test)
			for (x = 0; x < bitblocks; x++) {
				bh = bget(sdp, rl->start + x);
				if (x) {
					mh.mh_blkno = rl->start + x;
					gfs2_meta_header_out(&mh, bh->b_data);
				} else
					gfs2_rgrp_out(rg, bh->b_data);
				brelse(bh);
			}

		if (sdp->debug) {
			printf("\n");
			gfs2_rindex_print(ri);
		}

		sdp->blks_total += rgblocks;
		sdp->fssize = ri->ri_data0 + ri->ri_data;
	}
}


