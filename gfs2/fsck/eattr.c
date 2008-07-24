#include <stdint.h>
#include <string.h>
#include <linux_endian.h>

#include "libgfs2.h"
#include "fsck.h"

static int clear_blk_nodup(struct gfs2_sbd *sbp, uint64_t block)
{
	struct gfs2_block_query q;

	if(gfs2_block_check(sbp, bl, block, &q)) {
		stack;
		return -1;
	}

	if(q.dup_block) {
		log_debug("Not clearing block with marked as a duplicate\n");
		return 1;
	}

	gfs2_block_set(sbp, bl, block, gfs2_block_free);

	return 0;

}

int clear_eattr_indir(struct gfs2_inode *ip, uint64_t block,
		      uint64_t parent, struct gfs2_buffer_head **bh,
		      enum update_flags *want_updated, void *private)
{
	*want_updated = not_updated;
	return clear_blk_nodup(ip->i_sbd, block);
}

int clear_eattr_leaf(struct gfs2_inode *ip, uint64_t block,
		     uint64_t parent, struct gfs2_buffer_head **bh,
		     enum update_flags *want_updated, void *private)
{
	*want_updated = not_updated;
	return clear_blk_nodup(ip->i_sbd, block);
}

int clear_eattr_entry (struct gfs2_inode *ip,
		       struct gfs2_buffer_head *leaf_bh,
		       struct gfs2_ea_header *ea_hdr,
		       struct gfs2_ea_header *ea_hdr_prev,
		       void *private)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	char ea_name[256];

	if(!ea_hdr->ea_name_len){
		/* Skip this entry for now */
		return 1;
	}

	memset(ea_name, 0, sizeof(ea_name));
	strncpy(ea_name, (char *)ea_hdr + sizeof(struct gfs2_ea_header),
		ea_hdr->ea_name_len);

	if(!GFS2_EATYPE_VALID(ea_hdr->ea_type) &&
	   ((ea_hdr_prev) || (!ea_hdr_prev && ea_hdr->ea_type))){
		/* Skip invalid entry */
		return 1;
	}

	if(ea_hdr->ea_num_ptrs){
		uint32_t avail_size;
		int max_ptrs;

		avail_size = sdp->sd_sb.sb_bsize - sizeof(struct gfs2_meta_header);
		max_ptrs = (be32_to_cpu(ea_hdr->ea_data_len)+avail_size-1)/avail_size;

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

int clear_eattr_extentry(struct gfs2_inode *ip, uint64_t *ea_data_ptr,
			 struct gfs2_buffer_head *leaf_bh,
			 struct gfs2_ea_header *ea_hdr,
			 struct gfs2_ea_header *ea_hdr_prev,
			 enum update_flags *want_updated, void *private)
{
	uint64_t block = be64_to_cpu(*ea_data_ptr);

	*want_updated = not_updated;
	return clear_blk_nodup(ip->i_sbd, block);

}



