#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include "libgfs.h"
#include "gfs_ondisk.h"

#define FREE		(0x0)  /*   0000 */
#define BLOCK_IN_USE	(0x1)  /*   0001 */

#define DIR_INDIR_BLK	(0x2)  /*   0010 */
#define DIR_INODE	(0x3)  /*   0011 */
#define FILE_INODE	(0x4)  /*   0100 */
#define LNK_INODE	(0x5)
#define BLK_INODE	(0x6)
#define CHR_INODE	(0x7)
#define FIFO_INODE	(0x8)
#define SOCK_INODE	(0x9)
#define DIR_LEAF_INODE  (0xA)  /*   1010 */
#define JOURNAL_BLK     (0xB)  /*   1011 */
#define OTHER_META	(0xC)  /*   1100 */
#define FREE_META	(0xD)  /*   1101 */
#define EATTR_META	(0xE)  /*   1110 */

#define INVALID_META	(0xF)  /*   1111 */


/* Must be kept in sync with mark_block enum in block_list.h */
/* FIXME: Fragile */
static int mark_to_gbmap[16] = {
	FREE, BLOCK_IN_USE, DIR_INDIR_BLK, DIR_INODE, FILE_INODE,
	LNK_INODE, BLK_INODE, CHR_INODE, FIFO_INODE, SOCK_INODE,
	DIR_LEAF_INODE, JOURNAL_BLK, OTHER_META, FREE_META,
	EATTR_META, INVALID_META
};

struct block_list *block_list_create(uint64_t size, enum block_list_type type)
{
	struct block_list *il;
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
				stack;
				free(il);
				il = NULL;
			}
			if(bitmap_create(&il->list.gbmap.bad_map, size, 1)) {
				stack;
				free(il);
				il = NULL;
			}
			if(bitmap_create(&il->list.gbmap.dup_map, size, 1)) {
				stack;
				free(il);
				il = NULL;
			}
			if(bitmap_create(&il->list.gbmap.eattr_map, size, 1)) {
				stack;
				free(il);
				il = NULL;
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
				if(bitmap_get(&il->list.gbmap.dup_map, i,
					      &val)) {
					stack;
					return -1;
				}

				if(val)
					found = 1;
				break;
			case eattr_block:
				if(bitmap_get(&il->list.gbmap.eattr_map, i,
					      &val)) {
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
