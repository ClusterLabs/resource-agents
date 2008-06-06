#include <stdint.h>

#include "fsck_incore.h"


static int clear_blk_nodup(struct fsck_sb *sbp, uint64_t block)
{
	struct block_query q;

	if(block_check(sbp->bl, block, &q)) {
		stack;
		return -1;
	}

	if(q.dup_block) {
		log_debug("Not clearing block with marked as a duplicate\n");
		return 1;
	}

	block_set(sbp->bl, block, block_free);

	return 0;

}

int clear_eattr_indir(struct fsck_inode *ip, uint64_t block,
		      uint64_t parent, osi_buf_t **bh,
		      void *private)
{
	return clear_blk_nodup(ip->i_sbd, block);
}


int clear_eattr_leaf(struct fsck_inode *ip, uint64_t block,
		     uint64_t parent, osi_buf_t **bh,
		     void *private)
{

	return clear_blk_nodup(ip->i_sbd, block);

}


int clear_eattr_entry (struct fsck_inode *ip,
		       osi_buf_t *leaf_bh,
		       struct gfs_ea_header *ea_hdr,
		       struct gfs_ea_header *ea_hdr_prev,
		       void *private)
{
	struct fsck_sb *sdp = ip->i_sbd;
	char ea_name[256];

	if(!ea_hdr->ea_name_len){
		/* Skip this entry for now */
		return 1;
	}

	memset(ea_name, 0, sizeof(ea_name));
	strncpy(ea_name, (char *)ea_hdr + sizeof(struct gfs_ea_header),
		ea_hdr->ea_name_len);

	if(!GFS_EATYPE_VALID(ea_hdr->ea_type) &&
	   ((ea_hdr_prev) || (!ea_hdr_prev && ea_hdr->ea_type))){
		/* Skip invalid entry */
		return 1;
	}

	if(ea_hdr->ea_num_ptrs){
		uint32 avail_size;
		int max_ptrs;

		avail_size = sdp->sb.sb_bsize - sizeof(struct gfs_meta_header);
		max_ptrs = (gfs32_to_cpu(ea_hdr->ea_data_len)+avail_size-1)/avail_size;

		if(max_ptrs > ea_hdr->ea_num_ptrs) {
			return 1;
		} else {
			log_debug("  Pointers Required: %d\n"
				  "  Pointers Reported: %d\n",
				  max_ptrs,
				  ea_hdr->ea_num_ptrs);
		}


	}
	return 0;
}

int clear_eattr_extentry(struct fsck_inode *ip, uint64_t *ea_data_ptr,
			 osi_buf_t *leaf_bh, struct gfs_ea_header *ea_hdr,
			 struct gfs_ea_header *ea_hdr_prev, void *private)
{
	uint64_t block = gfs64_to_cpu(*ea_data_ptr);

	return clear_blk_nodup(ip->i_sbd, block);

}



