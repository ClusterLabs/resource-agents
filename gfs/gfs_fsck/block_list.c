#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "bitmap.h"
#include "block_list.h"
#include "fsck.h"

/* Must be kept in sync with mark_block enum in block_list.h */
static int mark_to_gbmap[16] = {
	block_free, block_used, indir_blk, inode_dir, inode_file,
	inode_lnk, inode_blk, inode_chr, inode_fifo, inode_sock,
	leaf_blk, journal_blk, meta_other, meta_free,
	meta_eattr, meta_inval
};

struct block_list *block_list_create(uint64_t size, enum block_list_type type)
{
	struct block_list *il;
	uint64_t addl_mem_needed = 0L;
	log_info("Creating a block list of size %"PRIu64"...\n", size);

	if ((il = malloc(sizeof(*il)))) {
		if(!memset(il, 0, sizeof(*il))) {
			log_err("Cannot set block list to zero\n");
			return NULL;
		}
		il->type = type;

		switch(type) {
		case gbmap:
			if(bitmap_create(&il->list.gbmap.group_map, size, 4)) {
				/* Note on addl_mem_needed: We've tried to allocate ram   */
				/* for our bitmaps, but we failed.  The fs is too big.    */
				/* We should tell them how much to allocate.  This first  */
				/* bitmap is the biggest, but we need three more smaller  */
				/* for the code that immediately follows.  I'm rounding   */
				/* up to twice the memory for this bitmap, even though    */
				/* it's actually 1 + 3/4.  That will allow for future     */
				/* mallocs that happen after this point in the code.      */
				/* For the bad_map, we have two more to go (total of 3)   */
				/* but again I'm rounding it up to 4 smaller ones.        */
				/* For the dup_map, I'm rounding from 2 to 3, and for     */
				/* eattr_map, I'm rounding up from 1 to 2.                */
				addl_mem_needed = il->list.gbmap.group_map.mapsize * 2;
				stack;
				free(il);
				il = NULL;
			}
			else if(bitmap_create(&il->list.gbmap.bad_map, size, 1)) {
				addl_mem_needed = il->list.gbmap.group_map.mapsize * 4;
				stack;
				free(il);
				il = NULL;
			}
			else if(bitmap_create(&il->list.gbmap.dup_map, size, 1)) {
				addl_mem_needed = il->list.gbmap.group_map.mapsize * 3;
				stack;
				free(il);
				il = NULL;
			}
			else if(bitmap_create(&il->list.gbmap.eattr_map, size, 1)) {
				addl_mem_needed = il->list.gbmap.group_map.mapsize * 2;
				stack;
				free(il);
				il = NULL;
			}
			if (addl_mem_needed) {
				log_err("This system doesn't have enough memory + swap space to fsck this file system.\n");
				log_err("Additional memory needed is approximately: %ldMB\n", addl_mem_needed / 1048576);
				log_err("Please increase your swap space by that amount and run gfs_fsck again.\n");
			}
			break;
		default:
			log_crit("Block list type %d not implemented\n",
				type);
			break;
		}
	}

	return il;
}

int block_mark(struct block_list *il, uint64_t block, enum mark_block mark)
{
	int err = 0;

	switch(il->type) {
	case gbmap:
		if(mark == bad_block) {
			err = bitmap_set(&il->list.gbmap.bad_map, block, 1);
		}
		else if(mark == dup_block) {
			err = bitmap_set(&il->list.gbmap.dup_map, block, 1);
		}
		else if(mark == eattr_block) {
			err = bitmap_set(&il->list.gbmap.eattr_map, block, 1);
		}
		else {
			err = bitmap_set(&il->list.gbmap.group_map, block,
					 mark_to_gbmap[mark]);
		}

		break;
	default:
		log_err("block list type %d not implemented\n",
			il->type);
		err = -1;
		break;
	}
	return err;
}

int block_set(struct block_list *il, uint64_t block, enum mark_block mark)
{
	int err = 0;
	err = block_clear(il, block, mark);
	if(!err)
		err = block_mark(il, block, mark);
	return err;
}

int block_clear(struct block_list *il, uint64_t block, enum mark_block m)
{
	int err = 0;

	switch(il->type) {
	case gbmap:
		switch (m) {
		case dup_block:
			err = bitmap_clear(&il->list.gbmap.dup_map, block);
			break;
		case bad_block:
			err = bitmap_clear(&il->list.gbmap.bad_map, block);
			break;
		case eattr_block:
			err = bitmap_clear(&il->list.gbmap.eattr_map, block);
			break;
		default:
			/* FIXME: check types */
			err = bitmap_clear(&il->list.gbmap.group_map, block);
			break;
		}

		break;
	default:
		log_err("block list type %d not implemented\n",
			il->type);
		err = -1;
		break;
	}
	return err;
}

int block_check(struct block_list *il, uint64_t block, struct block_query *val)
{
	int err = 0;
	val->block_type = 0;
	val->bad_block = 0;
	val->dup_block = 0;
	switch(il->type) {
	case gbmap:
		if((err = bitmap_get(&il->list.gbmap.group_map, block,
				     &val->block_type))) {
			log_err("Unable to get block type for block %"
				PRIu64"\n", block);
			break;
		}
		if((err = bitmap_get(&il->list.gbmap.bad_map, block,
				     &val->bad_block))) {
			log_err("Unable to get bad block status for block %"
				PRIu64"\n", block);
			break;
		}
		if((err = bitmap_get(&il->list.gbmap.dup_map, block,
				     &val->dup_block))) {
			log_err("Unable to get duplicate status for block %"
				PRIu64"\n", block);
			break;
		}
		if((err = bitmap_get(&il->list.gbmap.eattr_map, block,
				     &val->eattr_block))) {
			log_err("Unable to get eattr status for block %"
				PRIu64"\n", block);
			break;
		}
		break;
	default:
		log_err("block list type %d not implemented\n",
			il->type);
		err = -1;
		break;
	}

	return err;
}

void *block_list_destroy(struct block_list *il)
{
	if(il) {
		switch(il->type) {
		case gbmap:
			bitmap_destroy(&il->list.gbmap.group_map);
			bitmap_destroy(&il->list.gbmap.bad_map);
			bitmap_destroy(&il->list.gbmap.dup_map);
			bitmap_destroy(&il->list.gbmap.eattr_map);
			break;
		default:
			break;
		}
		free(il);
		il = NULL;
	}
	return il;
}


int find_next_block_type(struct block_list *il, enum mark_block m, uint64_t *b)
{
	uint64_t i;
	uint8_t val;
	int found = 0;
	for(i = *b; ; i++) {
		switch(il->type) {
		case gbmap:
			if(i >= bitmap_size(&il->list.gbmap.dup_map))
				return -1;

			switch(m) {
			case dup_block:
				if(bitmap_get(&il->list.gbmap.dup_map, i, &val)) {
					stack;
					return -1;
				}

				if(val)
					found = 1;
				break;
			case eattr_block:
				if(bitmap_get(&il->list.gbmap.eattr_map, i, &val)) {
					stack;
					return -1;
				}

				if(val)
					found = 1;
				break;
			default:
				/* FIXME: add support for getting
				 * other types */
				log_err("Unhandled block type\n");
			}
			break;
		default:
			log_err("Unhandled block list type\n");
			break;
		}
		if(found) {
			*b = i;
			return 0;
		}
	}
	return -1;
}
