#include <stdint.h>
#include "gfs_ondisk.h"
#include "incore.h"
#include "libgfs.h"

/**
 * fs_compute_bitstructs - Compute the bitmap sizes
 * @rgd: The resource group descriptor
 *
 * Returns: 0 on success, -1 on error
 */
int fs_compute_bitstructs(struct gfs_rgrpd *rgd)
{
	struct gfs_sbd *sdp = rgd->rd_sbd;
	struct gfs_bitmap *bits;
	uint32 length = rgd->rd_ri.ri_length;
	uint32 bytes_left, bytes;
	int x;

	if(!(rgd->rd_bits = (struct gfs_bitmap *)malloc(length * sizeof(struct gfs_bitmap)))) {
		log_err("Unable to allocate bitmap structure\n");
		stack;
		return -1;
	}
	if(!memset(rgd->rd_bits, 0, length * sizeof(struct gfs_bitmap))) {
		log_err("Unable to zero bitmap structure\n");
		stack;
		return -1;
	}
	
	bytes_left = rgd->rd_ri.ri_bitbytes;

	for (x = 0; x < length; x++){
		bits = &rgd->rd_bits[x];

		if (length == 1){
			bytes = bytes_left;
			bits->bi_offset = sizeof(struct gfs_rgrp);
			bits->bi_start = 0;
			bits->bi_len = bytes;
		}
		else if (x == 0){
			bytes = sdp->sd_sb.sb_bsize - sizeof(struct gfs_rgrp);
			bits->bi_offset = sizeof(struct gfs_rgrp);
			bits->bi_start = 0;
			bits->bi_len = bytes;
		}
		else if (x + 1 == length){
			bytes = bytes_left;
			bits->bi_offset = sizeof(struct gfs_meta_header);
			bits->bi_start = rgd->rd_ri.ri_bitbytes - bytes_left;
			bits->bi_len = bytes;
		}
		else{
			bytes = sdp->sd_sb.sb_bsize - sizeof(struct gfs_meta_header);
			bits->bi_offset = sizeof(struct gfs_meta_header);
			bits->bi_start = rgd->rd_ri.ri_bitbytes - bytes_left;
			bits->bi_len = bytes;
		}

		bytes_left -= bytes;
	}

	if(bytes_left){
		log_err( "fs_compute_bitstructs:  Too many blocks in rgrp to "
			"fit into available bitmap.\n");
		return -1;
	}

	if((rgd->rd_bits[length - 1].bi_start +
	    rgd->rd_bits[length - 1].bi_len) * GFS_NBBY != rgd->rd_ri.ri_data){
		log_err( "fs_compute_bitstructs:  # of blks in rgrp do not equal "
			"# of blks represented in bitmap.\n"
			"\tbi_start = %u\n"
			"\tbi_len   = %u\n"
			"\tGFS_NBBY = %u\n"
			"\tri_data  = %u\n",
			rgd->rd_bits[length - 1].bi_start,
			rgd->rd_bits[length - 1].bi_len,
			GFS_NBBY,
			rgd->rd_ri.ri_data);
		return -1;
	}


	if(!(rgd->rd_bh = (osi_buf_t **)malloc(length * sizeof(osi_buf_t *)))) {
		log_err("Unable to allocate osi_buf structure\n");
		stack;
		return -1;
	}
	if(!memset(rgd->rd_bh, 0, length * sizeof(osi_buf_t *))) {
		log_err("Unable to zero osi_buf structure\n");
		stack;
		return -1;
	}

	return 0;
}


/**
 * blk2rgrpd - Find resource group for a given data block number
 * @sdp: The GFS superblock
 * @n: The data block number
 *
 * Returns: Ths resource group, or NULL if not found
 */
struct gfs_rgrpd *fs_blk2rgrpd(struct gfs_sbd *sdp, uint64 blk)
{
	osi_list_t *tmp;
	struct gfs_rgrpd *rgd = NULL;
	struct gfs_rindex *ri;

	for(tmp = (osi_list_t *)sdp->sd_rglist.next; 
		tmp != (osi_list_t *)&sdp->sd_rglist; tmp = tmp->next){
		rgd = osi_list_entry(tmp, struct gfs_rgrpd, rd_list);
		ri = &rgd->rd_ri;

		if (ri->ri_data1 <= blk && blk < ri->ri_data1 + ri->ri_data){
			break;
		} else
			rgd = NULL;
	}
	return rgd;
}

/**
 * fs_rgrp_read - read in the resource group information from disk.
 * @rgd - resource group structure
 * @repair_if_corrupted - If TRUE, rgrps found to be corrupt should be repaired
 *                 according to the index.  If FALSE, no corruption is fixed.
 */
int fs_rgrp_read(int disk_fd, struct gfs_rgrpd *rgd, int repair_if_corrupted)
{
	struct gfs_sbd *sdp = rgd->rd_sbd;
	unsigned int x, length = rgd->rd_ri.ri_length;
	int error;

	if(rgd->rd_open_count){
		log_debug("rgrp already read...\n");
		rgd->rd_open_count++;
		return 0;
	}

	for (x = 0; x < length; x++){
		if(rgd->rd_bh[x]){
			log_err("Programmer error!  Bitmaps are already present in rgrp.\n");
			exit(1);
		}
		error = get_and_read_buf(disk_fd, sdp->sd_sb.sb_bsize,
								 rgd->rd_ri.ri_addr + x, &(rgd->rd_bh[x]), 0);
		if (error) {
		  	log_err("Unable to read rgrp from disk.\n"); 
		  	goto fail;
		}

		if(check_meta(rgd->rd_bh[x], (x) ? GFS_METATYPE_RB : GFS_METATYPE_RG)){
			log_err("Buffer #%"PRIu64" (%d of %d) is neither"
				" GFS_METATYPE_RB nor GFS_METATYPE_RG.\n",
				BH_BLKNO(rgd->rd_bh[x]),
				(int)x+1,
				(int)length);
			if (repair_if_corrupted) {
				log_err("Attempting to repair.\n");
				memset(&rgd->rd_rg, 0, sizeof(struct gfs_rgrp));
				rgd->rd_rg.rg_header.mh_magic = GFS_MAGIC;
				rgd->rd_rg.rg_header.mh_type = GFS_METATYPE_RG;
				rgd->rd_rg.rg_header.mh_format = GFS_FORMAT_RG;
				rgd->rd_rg.rg_free = rgd->rd_ri.ri_data;
				gfs_rgrp_out(&rgd->rd_rg, BH_DATA(rgd->rd_bh[x]));
				write_buf(disk_fd, rgd->rd_bh[x], BW_WAIT);
			}
			else {
				error = -1;
				goto fail;
			}
		}
	}

	gfs_rgrp_in(&rgd->rd_rg, BH_DATA(rgd->rd_bh[0]));
	rgd->rd_open_count = 1;

	return 0;

 fail:
	for (x = 0; x < length; x++){
		relse_buf(rgd->rd_bh[x]);
		rgd->rd_bh[x] = NULL;
	}

	log_err("Resource group is corrupted.\n");
	return error;
}

void fs_rgrp_relse(struct gfs_rgrpd *rgd)
{
	int x, length = rgd->rd_ri.ri_length;

	rgd->rd_open_count--;
	if(rgd->rd_open_count){
		log_debug("rgrp still held...\n");
	} else {
		for (x = 0; x < length; x++){
			relse_buf(rgd->rd_bh[x]);
			rgd->rd_bh[x] = NULL;
		}
	}
}


/**
 * rgrp_verify - Verify that a resource group is consistent
 * @sdp: the filesystem
 * @rgd: the rgrp
 *
 * Returns: 0 if ok, -1 on error
 */
int fs_rgrp_verify(struct gfs_rgrpd *rgd)
{
	struct gfs_bitmap *bits = NULL;
	uint32 length = rgd->rd_ri.ri_length;
	uint32 count[4], tmp;
	int buf, x;

	for (x = 0; x < 4; x++){
		count[x] = 0;

		for (buf = 0; buf < length; buf++){
			bits = &rgd->rd_bits[buf];
			count[x] += fs_bitcount((unsigned char *)
						BH_DATA(rgd->rd_bh[buf]) +
						bits->bi_offset,
						bits->bi_len, x);
		}
	}

	if(count[0] != rgd->rd_rg.rg_free){
		log_err("free data mismatch:  %u != %u\n",
			count[0], rgd->rd_rg.rg_free);
		return -1;
	}

	tmp = rgd->rd_ri.ri_data -
		(rgd->rd_rg.rg_usedmeta + rgd->rd_rg.rg_freemeta) -
		(rgd->rd_rg.rg_useddi + rgd->rd_rg.rg_freedi) -
		rgd->rd_rg.rg_free;

	if(count[1] != tmp){
		log_err("used data mismatch:  %u != %u\n",
			count[1], tmp);
		return -1;
	}
	if(count[2] != rgd->rd_rg.rg_freemeta){
		log_err("free metadata mismatch:  %u != %u\n",
			count[2], rgd->rd_rg.rg_freemeta);
		return -1;
	}

	tmp = rgd->rd_rg.rg_usedmeta +
		(rgd->rd_rg.rg_useddi + rgd->rd_rg.rg_freedi);

	if(count[3] != tmp){
		log_err("used metadata mismatch:  %u != %u\n",
			count[3], tmp);
		return -1;
	}
	return 0;
}

/**
 * fs_rgrp_recount - adjust block tracking numbers
 * rgd: resource group
 *
 * The resource groups keep track of how many free blocks, used blocks,
 * etc there are.  This function readjusts those numbers based on the
 * current state of the bitmap.
 *
 * Returns: 0 on success, -1 on failure
 */
int fs_rgrp_recount(int disk_fd, struct gfs_rgrpd *rgd){
	int i,j;
	fs_bitmap_t *bits = NULL;
	uint32 length = rgd->rd_ri.ri_length;
	uint32 count[4], tmp;

	for(i=0; i < 4; i++){
		count[i] = 0;
		for(j = 0; j < length; j++){
			bits = (fs_bitmap_t *)&rgd->rd_bits[j];
			count[i] += fs_bitcount((unsigned char *)
						BH_DATA(rgd->rd_bh[j]) +
						bits->bi_offset,
						bits->bi_len, i);
		}
	}
	if(count[0] != rgd->rd_rg.rg_free){
		log_warn("\tAdjusting free block count (%u -> %u).\n",
			rgd->rd_rg.rg_free, count[0]);
		rgd->rd_rg.rg_free = count[0];
	}
	if(count[2] != rgd->rd_rg.rg_freemeta){
		log_warn("\tAdjusting freemeta block count (%u -> %u).\n",
		       rgd->rd_rg.rg_freemeta, count[2]);
		rgd->rd_rg.rg_freemeta = count[2];
	}
	tmp = rgd->rd_rg.rg_usedmeta +
		(rgd->rd_rg.rg_useddi + rgd->rd_rg.rg_freedi);

	if(count[3] != tmp){
		int first = 1;
		struct gfs_sbd *sdp = rgd->rd_sbd;
		uint32 useddi = 0;
		uint32 freedi = 0;
		uint64 block;
		struct gfs_inode *ip;

		while (1){  /* count the used dinodes */
			if(next_rg_metatype(disk_fd, rgd, &block,
					    GFS_METATYPE_DI, first)){
				break;
			}
			first = 0;
			if(load_inode(disk_fd, sdp, block, &ip)) {
				stack;
				continue;
			}

			if (ip->i_di.di_flags & GFS_DIF_UNUSED){
				freedi++;
				continue;
			}
			free_inode(&ip);
			useddi++;
		}

		if(useddi != rgd->rd_rg.rg_useddi){
			log_warn("\tAdjusting used dinode block count (%u -> %u).\n",
				rgd->rd_rg.rg_useddi, useddi);
			rgd->rd_rg.rg_useddi = useddi;
		}
		if(freedi != rgd->rd_rg.rg_freedi){
			log_warn("\tAdjusting free dinode block count (%u -> %u).\n",
				rgd->rd_rg.rg_freedi, freedi);
			rgd->rd_rg.rg_freedi = freedi;
		}
		if(rgd->rd_rg.rg_usedmeta != count[3] - (freedi + useddi)){
			log_warn("\tAdjusting used meta block count (%u -> %u).\n",
				rgd->rd_rg.rg_usedmeta,
				(count[3] - (freedi + useddi)));
			rgd->rd_rg.rg_usedmeta = count[3] - (freedi + useddi);
		}
	}

	tmp = rgd->rd_ri.ri_data -
		(rgd->rd_rg.rg_usedmeta + rgd->rd_rg.rg_freemeta) -
		(rgd->rd_rg.rg_useddi + rgd->rd_rg.rg_freedi) -
		rgd->rd_rg.rg_free;

	if(count[1] != tmp){
		log_err("Could not reconcile rgrp block counts.\n");
		return -1;
	}
	return 0;
}

/**
 * clump_alloc - Allocate a clump of metadata
 * @rgd: the resource group descriptor
 * @goal: the goal block in the RG
 *
 * Returns: 0 on success, -1 on failure
 */
int clump_alloc(int disk_fd, struct gfs_rgrpd *rgd, uint32 goal)
{
	struct gfs_sbd *sdp = rgd->rd_sbd;
	struct gfs_meta_header mh;
	osi_buf_t *bh[GFS_META_CLUMP] = {0};
	uint32 block;
	int i,j;
	int error = 0;

	memset(&mh, 0, sizeof(struct gfs_meta_header));
	mh.mh_magic = GFS_MAGIC;
	mh.mh_type = GFS_METATYPE_NONE;

	if(rgd->rd_rg.rg_free < GFS_META_CLUMP){
		log_debug(" Not enough free blocks in rgrp.\n");
		return -1;
	}

	for (i = 0; i < GFS_META_CLUMP; i++){
		block = fs_blkalloc_internal(rgd, goal,
					     GFS_BLKST_FREE,
					     GFS_BLKST_FREEMETA, TRUE);
		log_debug("Got block %u\n", block);

		if(block == BFITNOENT) {
			log_err("Unable to get enough blocks\n");
			goto fail;
		}
		block += rgd->rd_ri.ri_data1;
		block_set(rgd->rd_sbd->bl, block, meta_free);
		if (get_buf(sdp->sd_sb.sb_bsize, block, &(bh[i]))){
			log_err("Unable to allocate new buffer.\n");
			goto fail;
		}
		gfs_meta_header_out(&mh, BH_DATA(bh[i]));

		goal = block;
	}

	log_debug("64 Meta blocks (%"PRIu64" - %"PRIu64"), allocated in rgrp 0x%lx\n",
		(rgd->rd_ri.ri_data1 + block)-63,
		(rgd->rd_ri.ri_data1 + block),
		(unsigned long)rgd);
	for (j = 0; j < GFS_META_CLUMP; j++){

		error = write_buf(disk_fd, bh[j], BW_WAIT);
		if (error){
			log_err("Unable to write allocated metablock to disk.\n");
			goto fail;
		}
	}

	if(rgd->rd_rg.rg_free < GFS_META_CLUMP){
		log_err("More blocks were allocated from rgrp "
			"than are available.\n");
		goto fail;
	}
	rgd->rd_rg.rg_free -= GFS_META_CLUMP;
	rgd->rd_rg.rg_freemeta += GFS_META_CLUMP;

	for (i = 0; i < GFS_META_CLUMP; i++)
		relse_buf(bh[i]);

	return 0;

 fail:
	log_debug("clump_alloc failing...\n");
	for(--i; i >=0; i--){
		fs_set_bitmap(disk_fd, sdp, BH_BLKNO(bh[i]), GFS_BLKST_FREE);
		relse_buf(bh[i]);
	}
	return -1;
}


/**
 * fs_blkalloc - Allocate a data block
 * @ip: the inode to allocate the data block for
 * @block: the block allocated
 *
 * Returns: 0 on success, -1 on failure
 */
int fs_blkalloc(int disk_fd, struct gfs_inode *ip, uint64 *block)
{
	osi_list_t *tmp;
	struct gfs_sbd *sdp = ip->i_sbd;
	struct gfs_rgrpd *rgd;
	uint32 goal;
	int same;

	for(tmp = (osi_list_t *)sdp->sd_rglist.next;
		tmp != (osi_list_t *)&sdp->sd_rglist; tmp = tmp->next){
		rgd = osi_list_entry(tmp, struct gfs_rgrpd, rd_list);

		if(!rgd){
			log_err( "fs_blkalloc:  Bad rgrp list!\n");
			return -1;
		}

		if(fs_rgrp_read(disk_fd, rgd, FALSE)){
			log_err( "fs_blkalloc:  Unable to read rgrp.\n");
			return -1;
		}

		if(!rgd->rd_rg.rg_free){
			fs_rgrp_relse(rgd);
			continue;
		}

		same = (rgd->rd_ri.ri_addr == ip->i_di.di_goal_rgrp);
		goal = (same) ? ip->i_di.di_goal_dblk : 0;

		*block = fs_blkalloc_internal(rgd, goal,
					      GFS_BLKST_FREE,
					      GFS_BLKST_USED, TRUE);

		log_debug("Got block %"PRIu64"\n", *block);
		if(*block == BFITNOENT) {
			fs_rgrp_relse(rgd);
			continue;
		}
		if (!same){
			ip->i_di.di_goal_rgrp = rgd->rd_ri.ri_addr;
			ip->i_di.di_goal_mblk = 0;
		}

		*block += rgd->rd_ri.ri_data1;
		ip->i_di.di_goal_dblk = *block;

		rgd->rd_rg.rg_free--;

		gfs_rgrp_out(&rgd->rd_rg, BH_DATA(rgd->rd_bh[0]));
		if(write_buf(disk_fd, rgd->rd_bh[0], 0)){
			log_err( "Unable to write out rgrp block #%"
				PRIu64".\n",
				BH_BLKNO(rgd->rd_bh[0]));
			fs_rgrp_relse(rgd);
			return -1;
		}
		fs_rgrp_relse(rgd);
		return 0;
	}

	return 1;
}


/**
 * fs_metaalloc - Allocate a metadata block to a file
 * @ip:  the file
 * @block: the block allocated
 *
 * Returns: 0 on success, -1 on failure
 */
int fs_metaalloc(int disk_fd, struct gfs_inode *ip, uint64 *block)
{
	osi_list_t *tmp;
	struct gfs_sbd *sdp = ip->i_sbd;
	struct gfs_rgrpd *rgd;
	uint32 goal;
	int same;
	int error = 0;

	/* ATTENTION -- maybe we should try to allocate from goal rgrp first */
	for(tmp = (osi_list_t *)sdp->sd_rglist.next;
		tmp != (osi_list_t *)&sdp->sd_rglist; tmp = tmp->next){
		rgd = osi_list_entry(tmp, struct gfs_rgrpd, rd_list);

		if(!rgd){
			log_err( "fs_metaalloc:  Bad rgrp list!\n");
			return -1;
		}

		if(fs_rgrp_read(disk_fd, rgd, FALSE)){
			log_err( "fs_metaalloc:  Unable to read rgrp.\n");
			return -1;
		}

		same = (rgd->rd_ri.ri_addr == ip->i_di.di_goal_rgrp);
		goal = (same) ? ip->i_di.di_goal_mblk : 0;

		if (!rgd->rd_rg.rg_freemeta){
			error = clump_alloc(disk_fd, rgd, goal);
			if (error){
				fs_rgrp_relse(rgd);
				continue;
			}
		}


		if(!rgd->rd_rg.rg_freemeta){
			fs_rgrp_relse(rgd);
			continue;
		}
		*block = fs_blkalloc_internal(rgd, goal,
					      GFS_BLKST_FREEMETA,
					      GFS_BLKST_USEDMETA, TRUE);
		log_debug("Got block %"PRIu64"\n", *block);
		if(*block == BFITNOENT) {
			fs_rgrp_relse(rgd);
			continue;
		}
		if (!same){
			ip->i_di.di_goal_rgrp = rgd->rd_ri.ri_addr;
			ip->i_di.di_goal_dblk = 0;
		}
		*block += rgd->rd_ri.ri_data1;
		ip->i_di.di_goal_mblk = *block;

		rgd->rd_rg.rg_freemeta--;
		rgd->rd_rg.rg_usedmeta++;

		gfs_rgrp_out(&rgd->rd_rg, BH_DATA(rgd->rd_bh[0]));
		write_buf(disk_fd, rgd->rd_bh[0], 0);
		fs_rgrp_relse(rgd);
		/* if we made it this far, then we are ok */
		return 0;
	}

	return -1;
}
