#include <inttypes.h>
#include <linux_endian.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libgfs2.h"
#include "fsck.h"
#include "osi_list.h"
#include "util.h"
#include "metawalk.h"
#include "inode_hash.h"

struct inode_with_dups {
	osi_list_t list;
	uint64_t block_no;
	int dup_count;
	int ea_only;
	uint64_t parent;
	char *name;
};

struct blocks {
	osi_list_t list;
	uint64_t block_no;
	osi_list_t ref_inode_list;
};

struct fxn_info {
	uint64_t block;
	int found;
	int ea_only;    /* The only dups were found in EAs */
};

struct dup_handler {
	struct blocks *b;
	struct inode_with_dups *id;
	int ref_inode_count;
	int ref_count;
};

static inline void inc_if_found(uint64_t block, int not_ea, void *private) {
	struct fxn_info *fi = (struct fxn_info *) private;
	if(block == fi->block) {
		(fi->found)++;
		if(not_ea)
			fi->ea_only = 0;
	}
}

static int check_metalist(struct gfs2_inode *ip, uint64_t block,
			  struct gfs2_buffer_head **bh, void *private)
{
	inc_if_found(block, 1, private);

	return 0;
}

static int check_data(struct gfs2_inode *ip, uint64_t block, void *private)
{
	inc_if_found(block, 1, private);

	return 0;
}

static int check_eattr_indir(struct gfs2_inode *ip, uint64_t block,
			     uint64_t parent, struct gfs2_buffer_head **bh, void *private)
{
	struct gfs2_sbd *sbp = ip->i_sbd;
	struct gfs2_buffer_head *indir_bh = NULL;

	inc_if_found(block, 0, private);
	indir_bh = bread(sbp, block);
	*bh = indir_bh;

	return 0;
}

static int check_eattr_leaf(struct gfs2_inode *ip, uint64_t block,
			    uint64_t parent, struct gfs2_buffer_head **bh, void *private)
{
	struct gfs2_sbd *sbp = ip->i_sbd;
	struct gfs2_buffer_head *leaf_bh = NULL;

	inc_if_found(block, 0, private);
	leaf_bh = bread(sbp, block);

	*bh = leaf_bh;
	return 0;
}

static int check_eattr_entry(struct gfs2_inode *ip,
							 struct gfs2_buffer_head *leaf_bh,
							 struct gfs2_ea_header *ea_hdr,
							 struct gfs2_ea_header *ea_hdr_prev,
							 void *private)
{
	return 0;
}

static int check_eattr_extentry(struct gfs2_inode *ip, uint64_t *ea_data_ptr,
				struct gfs2_buffer_head *leaf_bh,
				struct gfs2_ea_header *ea_hdr,
				struct gfs2_ea_header *ea_hdr_prev,
				void *private)
{
	uint64_t block = be64_to_cpu(*ea_data_ptr);

	inc_if_found(block, 0, private);

	return 0;
}

static int find_dentry(struct gfs2_inode *ip, struct gfs2_dirent *de,
		       struct gfs2_dirent *prev,
		       struct gfs2_buffer_head *bh, char *filename, int *update,
		       uint16_t *count, void *priv)
{
	osi_list_t *tmp1, *tmp2;
	struct blocks *b;
	struct inode_with_dups *id;
	struct gfs2_leaf leaf;

	osi_list_foreach(tmp1, &dup_list) {
		b = osi_list_entry(tmp1, struct blocks, list);
		osi_list_foreach(tmp2, &b->ref_inode_list) {
			id = osi_list_entry(tmp2, struct inode_with_dups,
					    list);
			if(id->name)
				/* We can only have one parent of
				 * inodes that contain duplicate
				 * blocks... */
				continue;
			if(id->block_no == de->de_inum.no_addr) {
				id->name = strdup(filename);
				id->parent = ip->i_di.di_num.no_addr;
				log_debug("Duplicate block %" PRIu64 " (0x%" PRIx64
						  ") is in file or directory %" PRIu64
						  " (0x%" PRIx64 ") named %s\n", id->block_no,
						  id->block_no, ip->i_di.di_num.no_addr,
						  ip->i_di.di_num.no_addr, filename);
				/* If there are duplicates of
				 * duplicates, I guess we'll miss them
				 * here */
				break;
			}
		}
	}
	/* Return the number of leaf entries so metawalk doesn't flag this
	   leaf as having none. */
	gfs2_leaf_in(&leaf, bh->b_data);
	*count = leaf.lf_entries;
	return 0;
}

static int clear_dup_metalist(struct gfs2_inode *ip, uint64_t block,
			      struct gfs2_buffer_head **bh, void *private)
{
	struct dup_handler *dh = (struct dup_handler *) private;

	if(dh->ref_count == 1)
		return 1;
	if(block == dh->b->block_no) {
		log_err("Found dup in inode \"%s\" (block #%"PRIu64
				") with block #%"PRIu64"\n",
				dh->id->name ? dh->id->name : "unknown name",
				ip->i_di.di_num.no_addr, block);
		log_err("Inode %s is in directory %"PRIu64" (0x%" PRIx64 ")\n",
				dh->id->name ? dh->id->name : "",
				dh->id->parent, dh->id->parent);
		inode_hash_remove(inode_hash, ip->i_di.di_num.no_addr);
		/* Setting the block to invalid means the inode is
		 * cleared in pass2 */
		gfs2_block_set(bl, ip->i_di.di_num.no_addr, gfs2_meta_inval);
	}
	return 0;
}

static int clear_dup_data(struct gfs2_inode *ip, uint64_t block, void *private)
{
	struct dup_handler *dh = (struct dup_handler *) private;

	if(dh->ref_count == 1) {
		return 1;
	}
	if(block == dh->b->block_no) {
		log_err("Found dup in inode \"%s\" for block #%" PRIu64
				" (0x%" PRIx64 ") at block #%" PRIu64 " (0x%" PRIx64 ")\n",
				dh->id->name ? dh->id->name : "unknown name",
				ip->i_di.di_num.no_addr, ip->i_di.di_num.no_addr, block,
				block);
		log_err("Inode %s is in directory %"PRIu64" (0x%" PRIx64 ")\n",
				dh->id->name ? dh->id->name : "", dh->id->parent,
				dh->id->parent);
		inode_hash_remove(inode_hash, ip->i_di.di_num.no_addr);
		/* Setting the block to invalid means the inode is
		 * cleared in pass2 */
		gfs2_block_set(bl, ip->i_di.di_num.no_addr, gfs2_meta_inval);
	}

	return 0;
}
static int clear_dup_eattr_indir(struct gfs2_inode *ip, uint64_t block,
				 uint64_t parent, struct gfs2_buffer_head **bh,
				 void *private)
{
	struct dup_handler *dh = (struct dup_handler *) private;
	/* Can't use fxns from eattr.c since we need to check the ref
	 * count */
	*bh = NULL;
	if(dh->ref_count == 1)
		return 1;
	if(block == dh->b->block_no) {
		log_err("Found dup in inode \"%s\" with address #%" PRIu64
				" (0x%" PRIx64 ") with block #%" PRIu64 " (0x%" PRIx64 ")\n",
				dh->id->name ? dh->id->name : "unknown name",
				ip->i_di.di_num.no_addr, ip->i_di.di_num.no_addr, block,
				block);
		log_err("Inode %s is in directory %" PRIu64 " (0x%" PRIx64 ")\n",
				dh->id->name ? dh->id->name : "",
				dh->id->parent, dh->id->parent);
		gfs2_block_set(bl, ip->i_di.di_eattr, gfs2_meta_inval);
	}

	return 0;
}

static int clear_dup_eattr_leaf(struct gfs2_inode *ip, uint64_t block,
				uint64_t parent, struct gfs2_buffer_head **bh, void *private)
{
	struct dup_handler *dh = (struct dup_handler *) private;
	if(dh->ref_count == 1)
		return 1;
	if(block == dh->b->block_no) {
		log_err("Found dup in inode \"%s\" with address #%" PRIu64
				" (0x%" PRIx64 ") with block #%" PRIu64 " (0x%" PRIx64 ")\n",
				dh->id->name ? dh->id->name : "unknown name",
				ip->i_di.di_num.no_addr, ip->i_di.di_num.no_addr, block,
				block);
		log_err("Inode %s is in directory %" PRIu64 " (0x%" PRIx64 ")\n",
				dh->id->name ? dh->id->name : "",
				dh->id->parent, dh->id->parent);
		/* mark the main eattr block invalid */
		gfs2_block_set(bl, ip->i_di.di_eattr, gfs2_meta_inval);
	}

	return 0;
}

static int clear_eattr_entry (struct gfs2_inode *ip,
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
		max_ptrs = (be32_to_cpu(ea_hdr->ea_data_len) + avail_size - 1) /
			avail_size;

		if(max_ptrs > ea_hdr->ea_num_ptrs)
			return 1;
		else {
			log_debug("  Pointers Required: %d\n  Pointers Reported: %d\n",
					  max_ptrs, ea_hdr->ea_num_ptrs);
		}
	}
	return 0;
}

static int clear_eattr_extentry(struct gfs2_inode *ip, uint64_t *ea_data_ptr,
			 struct gfs2_buffer_head *leaf_bh, struct gfs2_ea_header *ea_hdr,
			 struct gfs2_ea_header *ea_hdr_prev, void *private)
{
	uint64_t block = be64_to_cpu(*ea_data_ptr);
	struct dup_handler *dh = (struct dup_handler *) private;
	if(dh->ref_count == 1)
		return 1;
	if(block == dh->b->block_no) {
		log_err("Found dup in inode \"%s\" with address #%" PRIu64
				" (0x%" PRIx64 ") with block #%" PRIu64 " (0x%" PRIx64 ")\n",
				dh->id->name ? dh->id->name : "unknown name",
				ip->i_di.di_num.no_addr, ip->i_di.di_num.no_addr, block,
				block);
		log_err("Inode %s is in directory %" PRIu64 " (0x%" PRIx64 ")\n",
				dh->id->name ? dh->id->name : "",
				dh->id->parent, dh->id->parent);
		/* mark the main eattr block invalid */
		gfs2_block_set(bl, ip->i_di.di_eattr, gfs2_meta_inval);
	}

	return 0;

}

/* Finds all references to duplicate blocks in the metadata */
int find_block_ref(struct gfs2_sbd *sbp, uint64_t inode, struct blocks *b)
{
	struct gfs2_inode *ip;
	struct fxn_info myfi = {b->block_no, 0, 1};
	struct inode_with_dups *id = NULL;
	struct metawalk_fxns find_refs = {
		.private = (void*) &myfi,
		.check_leaf = NULL,
		.check_metalist = check_metalist,
		.check_data = check_data,
		.check_eattr_indir = check_eattr_indir,
		.check_eattr_leaf = check_eattr_leaf,
		.check_dentry = NULL,
		.check_eattr_entry = check_eattr_entry,
		.check_eattr_extentry = check_eattr_extentry,
	};

	ip = fsck_load_inode(sbp, inode); /* bread, inode_get */
	log_info("Checking inode %" PRIu64 " (0x%" PRIx64
			 ")'s metatree for references to block %" PRIu64 " (0x%" PRIx64
			 ")\n", inode, inode, b->block_no, b->block_no);
	if(check_metatree(ip, &find_refs)) {
		stack;
		fsck_inode_put(ip, not_updated); /* out, brelse, free */
		return -1;
	}
	log_info("Done checking metatree\n");
	/* Check for ea references in the inode */
	if(check_inode_eattr(ip, &find_refs) < 0){
		stack;
		fsck_inode_put(ip, not_updated); /* out, brelse, free */
		return -1;
	}
	if (myfi.found) {
		if(!(id = malloc(sizeof(*id)))) {
			log_crit("Unable to allocate inode_with_dups structure\n");
			return -1;
		}
		if(!(memset(id, 0, sizeof(*id)))) {
			log_crit("Unable to zero inode_with_dups structure\n");
			return -1;
		}
		log_debug("Found %d entries with block %" PRIu64
				  " (0x%" PRIx64 ") in inode #%" PRIu64 " (0x%" PRIx64 ")\n",
				  myfi.found, b->block_no, b->block_no, inode, inode);
		id->dup_count = myfi.found;
		id->block_no = inode;
		id->ea_only = myfi.ea_only;
		osi_list_add_prev(&id->list, &b->ref_inode_list);
	}
	fsck_inode_put(ip, (opts.no ? not_updated : updated)); /* out, brelse, free */
	return 0;
}

/* Finds all blocks marked in the duplicate block bitmap */
int find_dup_blocks(struct gfs2_sbd *sbp)
{
	uint64_t block_no = 0;
	struct blocks *b;

	while (!gfs2_find_next_block_type(bl, gfs2_dup_block, &block_no)) {
		if(!(b = malloc(sizeof(*b)))) {
			log_crit("Unable to allocate blocks structure\n");
			return -1;
		}
		if(!memset(b, 0, sizeof(*b))) {
			log_crit("Unable to zero blocks structure\n");
			return -1;
		}
		b->block_no = block_no;
		osi_list_init(&b->ref_inode_list);
		log_notice("Found dup block at %"PRIu64" (0x%" PRIx64 ")\n", block_no,
				   block_no);
		osi_list_add(&b->list, &dup_list);
		block_no++;
	}
	return 0;
}



int handle_dup_blk(struct gfs2_sbd *sbp, struct blocks *b)
{
	osi_list_t *tmp;
	struct inode_with_dups *id;
	struct metawalk_fxns clear_dup_fxns = {
		.private = NULL,
		.check_leaf = NULL,
		.check_metalist = clear_dup_metalist,
		.check_data = clear_dup_data,
		.check_eattr_indir = clear_dup_eattr_indir,
		.check_eattr_leaf = clear_dup_eattr_leaf,
		.check_dentry = NULL,
		.check_eattr_entry = clear_eattr_entry,
		.check_eattr_extentry = clear_eattr_extentry,
	};
	struct gfs2_inode *ip;
	struct dup_handler dh = {0};

	osi_list_foreach(tmp, &b->ref_inode_list) {
		id = osi_list_entry(tmp, struct inode_with_dups, list);
		dh.ref_inode_count++;
		dh.ref_count += id->dup_count;
	}
	log_notice("Block %" PRIu64 " (0x%" PRIx64 ") has %d inodes referencing it"
			   " for a total of %d duplicate references\n",
			   b->block_no, b->block_no, dh.ref_inode_count,
			   dh.ref_inode_count, dh.ref_count);

	osi_list_foreach(tmp, &b->ref_inode_list) {
		id = osi_list_entry(tmp, struct inode_with_dups, list);
		log_warn("Inode %s has %d reference(s) to block %"PRIu64
				 " (0x%" PRIx64 ")\n", id->name, id->dup_count, b->block_no,
				 b->block_no);
		/* FIXME: User input */
		log_warn("Clearing...\n");
		ip = fsck_load_inode(sbp, id->block_no);
		dh.b = b;
		dh.id = id;
		clear_dup_fxns.private = (void *) &dh;
		/* Clear the EAs for the inode first */
		check_inode_eattr(ip, &clear_dup_fxns);
		/* If the dup wasn't only in the EA, clear the inode */
		if(!id->ea_only)
			check_metatree(ip, &clear_dup_fxns);

		fsck_inode_put(ip, not_updated); /* out, brelse, free */
		dh.ref_inode_count--;
		if(dh.ref_inode_count == 1)
			break;
		/* Inode is marked invalid and is removed in pass2 */
		/* FIXME: other option should be to duplicate the
		 * block for each duplicate and point the metadata at
		 * the cloned blocks */
	}
	return 0;

}

/* Pass 1b handles finding the previous inode for a duplicate block
 * When found, store the inodes pointing to the duplicate block for
 * use in pass2 */
int pass1b(struct gfs2_sbd *sbp)
{
	struct blocks *b;
	uint64_t i;
	struct gfs2_block_query q;
	osi_list_t *tmp = NULL;
	struct metawalk_fxns find_dirents = {0};
	find_dirents.check_dentry = &find_dentry;
	int rc = 0;

	osi_list_init(&dup_list);
	/* Shove all blocks marked as duplicated into a list */
	log_info("Looking for duplicate blocks...\n");
	find_dup_blocks(sbp);

	/* If there were no dups in the bitmap, we don't need to do anymore */
	if(osi_list_empty(&dup_list)) {
		log_info("No duplicate blocks found\n");
		return 0;
	}

	/* Rescan the fs looking for pointers to blocks that are in
	 * the duplicate block map */
	log_info("Scanning filesystem for inodes containing duplicate blocks...\n");
	log_debug("Filesystem has %"PRIu64" (0x%" PRIx64 ") blocks total\n",
			  last_fs_block, last_fs_block);
	for(i = 0; i < last_fs_block; i += 1) {
		warm_fuzzy_stuff(i);
		if (skip_this_pass || fsck_abort) /* if asked to skip the rest */
			goto out;
		log_debug("Scanning block %" PRIu64 " (0x%" PRIx64 ") for inodes\n",
				  i, i);
		if(gfs2_block_check(bl, i, &q)) {
			stack;
			rc = -1;
			goto out;
		}
		if((q.block_type == gfs2_inode_dir) ||
		   (q.block_type == gfs2_inode_file) ||
		   (q.block_type == gfs2_inode_lnk) ||
		   (q.block_type == gfs2_inode_blk) ||
		   (q.block_type == gfs2_inode_chr) ||
		   (q.block_type == gfs2_inode_fifo) ||
		   (q.block_type == gfs2_inode_sock)) {
			osi_list_foreach(tmp, &dup_list) {
				b = osi_list_entry(tmp, struct blocks, list);
				if(find_block_ref(sbp, i, b)) {
					stack;
					rc = -1;
					goto out;
				}
			}
		}
		if(q.block_type == gfs2_inode_dir) {
			check_dir(sbp, i, &find_dirents);
		}
	}

	/* Fix dups here - it's going to slow things down a lot to fix
	 * it later */
	log_info("Handling duplicate blocks\n");
out:
	while (!osi_list_empty(&dup_list)) {
		b = osi_list_entry(dup_list.next, struct blocks, list);
		if (!skip_this_pass && !rc) /* no error & not asked to skip the rest */
			handle_dup_blk(sbp, b);
		osi_list_del(&b->list);
		free(b);
	}
	return rc;
}
