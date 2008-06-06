#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libgfs2.h"

/**
 * gfs2_compute_bitstructs - Compute the bitmap sizes
 * @rgd: The resource group descriptor
 *
 * Returns: 0 on success, -1 on error
 */
int gfs2_compute_bitstructs(struct gfs2_sbd *sdp, struct rgrp_list *rgd)
{
	struct gfs2_bitmap *bits;
	uint32_t length = rgd->ri.ri_length;
	uint32_t bytes_left, bytes;
	int x;

	if(!(rgd->bits = (struct gfs2_bitmap *)
		 malloc(length * sizeof(struct gfs2_bitmap))))
		return -1;
	if(!memset(rgd->bits, 0, length * sizeof(struct gfs2_bitmap)))
		return -1;
	
	bytes_left = rgd->ri.ri_bitbytes;

	for (x = 0; x < length; x++){
		bits = &rgd->bits[x];

		if (length == 1){
			bytes = bytes_left;
			bits->bi_offset = sizeof(struct gfs2_rgrp);
			bits->bi_start = 0;
			bits->bi_len = bytes;
		}
		else if (x == 0){
			bytes = sdp->sd_sb.sb_bsize - sizeof(struct gfs2_rgrp);
			bits->bi_offset = sizeof(struct gfs2_rgrp);
			bits->bi_start = 0;
			bits->bi_len = bytes;
		}
		else if (x + 1 == length){
			bytes = bytes_left;
			bits->bi_offset = sizeof(struct gfs2_meta_header);
			bits->bi_start = rgd->ri.ri_bitbytes - bytes_left;
			bits->bi_len = bytes;
		}
		else{
			bytes = sdp->sd_sb.sb_bsize - sizeof(struct gfs2_meta_header);
			bits->bi_offset = sizeof(struct gfs2_meta_header);
			bits->bi_start = rgd->ri.ri_bitbytes - bytes_left;
			bits->bi_len = bytes;
		}

		bytes_left -= bytes;
	}

	if(bytes_left)
		return -1;

	if((rgd->bits[length - 1].bi_start +
	    rgd->bits[length - 1].bi_len) * GFS2_NBBY != rgd->ri.ri_data)
		return -1;

	if(!(rgd->bh = (struct gfs2_buffer_head **)
		 malloc(length * sizeof(struct gfs2_buffer_head *))))
		return -1;
	if(!memset(rgd->bh, 0, length * sizeof(struct gfs2_buffer_head *)))
		return -1;

	return 0;
}


/**
 * blk2rgrpd - Find resource group for a given data block number
 * @sdp: The GFS superblock
 * @n: The data block number
 *
 * Returns: Ths resource group, or NULL if not found
 */
struct rgrp_list *gfs2_blk2rgrpd(struct gfs2_sbd *sdp, uint64_t blk)
{
	osi_list_t *tmp;
	struct rgrp_list *rgd = NULL;
	struct gfs2_rindex *ri;

	for(tmp = sdp->rglist.next; tmp != &sdp->rglist; tmp = tmp->next){
		rgd = osi_list_entry(tmp, struct rgrp_list, list);
		ri = &rgd->ri;

		if (ri->ri_data0 <= blk && blk < ri->ri_data0 + ri->ri_data){
			break;
		} else
			rgd = NULL;
	}
	return rgd;
}

/**
 * fs_rgrp_read - read in the resource group information from disk.
 * @rgd - resource group structure
 * returns: 0 if no error, otherwise the block number that failed
 */
uint64_t gfs2_rgrp_read(struct gfs2_sbd *sdp, struct rgrp_list *rgd)
{
	int x, length = rgd->ri.ri_length;

	for (x = 0; x < length; x++){
		rgd->bh[x] = bread(sdp, rgd->ri.ri_addr + x);
		if(gfs2_check_meta(rgd->bh[x],
				   (x) ? GFS2_METATYPE_RB : GFS2_METATYPE_RG))
		{
			uint64_t error;

			error = rgd->ri.ri_addr + x;
			for (; x >= 0; x--)
				brelse(rgd->bh[x], not_updated);
			return error;
		}
	}

	gfs2_rgrp_in(&rgd->rg, rgd->bh[0]->b_data);
	return 0;
}

void gfs2_rgrp_relse(struct rgrp_list *rgd, enum update_flags updated)
{
	int x, length = rgd->ri.ri_length;

	for (x = 0; x < length; x++)
		brelse(rgd->bh[x], updated);
}

void gfs2_rgrp_free(osi_list_t *rglist, enum update_flags updated)
{
	struct rgrp_list *rgd;

	while(!osi_list_empty(rglist->next)){
		rgd = osi_list_entry(rglist->next, struct rgrp_list, list);
		if (rgd->bh && rgd->bh[0] && /* if a buffer exists and       */
			rgd->bh[0]->b_count) /* the 1st buffer is allocated */
			gfs2_rgrp_relse(rgd, updated); /* free them all. */
		if(rgd->bits)
			free(rgd->bits);
		if(rgd->bh)
			free(rgd->bh);
		osi_list_del(&rgd->list);
		free(rgd);
	}
}
