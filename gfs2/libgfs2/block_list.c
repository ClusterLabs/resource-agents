#include <inttypes.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "libgfs2.h"

#define FREE	        (0x0)  /*   0000 */
#define BLOCK_IN_USE    (0x1)  /*   0001 */
#define DIR_INDIR_BLK   (0x2)  /*   0010 */
#define DIR_INODE       (0x3)  /*   0011 */
#define FILE_INODE      (0x4)  /*   0100 */
#define LNK_INODE       (0x5)
#define BLK_INODE       (0x6)
#define CHR_INODE       (0x7)
#define FIFO_INODE      (0x8)
#define SOCK_INODE      (0x9)
#define DIR_LEAF_INODE  (0xA)  /*   1010 */
#define JOURNAL_BLK     (0xB)  /*   1011 */
#define OTHER_META      (0xC)  /*   1100 */
#define EATTR_META      (0xD)  /*   1101 */
#define UNUSED1	        (0xE)  /*   1110 */
#define INVALID_META    (0xF)  /*   1111 */

/* Must be kept in sync with mark_block enum in block_list.h */
/* FIXME: Fragile */
static int mark_to_gbmap[16] = {
	FREE, BLOCK_IN_USE, DIR_INDIR_BLK, DIR_INODE, FILE_INODE,
	LNK_INODE, BLK_INODE, CHR_INODE, FIFO_INODE, SOCK_INODE,
	DIR_LEAF_INODE, JOURNAL_BLK, OTHER_META, EATTR_META,
	INVALID_META, INVALID_META
};

struct gfs2_block_list *gfs2_block_list_create(uint64_t size,
											   uint64_t *addl_mem_needed)
{
	struct gfs2_block_list *il;

	*addl_mem_needed = 0L;
	if ((il = malloc(sizeof(*il)))) {
		if(!memset(il, 0, sizeof(*il)))
			return NULL;

		if(gfs2_bitmap_create(&il->list.gbmap.group_map, size, 4)) {
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
			*addl_mem_needed = il->list.gbmap.group_map.mapsize * 2;
			free(il);
			il = NULL;
		}
		else if(gfs2_bitmap_create(&il->list.gbmap.bad_map, size, 1)) {
			*addl_mem_needed = il->list.gbmap.group_map.mapsize * 4;
			free(il);
			il = NULL;
		}
		else if(gfs2_bitmap_create(&il->list.gbmap.dup_map, size, 1)) {
			*addl_mem_needed = il->list.gbmap.group_map.mapsize * 3;
			free(il);
			il = NULL;
		}
		else if(gfs2_bitmap_create(&il->list.gbmap.eattr_map, size, 1)) {
			*addl_mem_needed = il->list.gbmap.group_map.mapsize * 2;
			free(il);
			il = NULL;
		}
	}
	return il;
}

int gfs2_block_mark(struct gfs2_block_list *il, uint64_t block,
					enum gfs2_mark_block mark)
{
	int err = 0;

	if(mark == gfs2_bad_block)
		err = gfs2_bitmap_set(&il->list.gbmap.bad_map, block, 1);
	else if(mark == gfs2_dup_block)
		err = gfs2_bitmap_set(&il->list.gbmap.dup_map, block, 1);
	else if(mark == gfs2_eattr_block)
		err = gfs2_bitmap_set(&il->list.gbmap.eattr_map, block, 1);
	else
		err = gfs2_bitmap_set(&il->list.gbmap.group_map, block,
							  mark_to_gbmap[mark]);
	return err;
}

int gfs2_block_set(struct gfs2_block_list *il, uint64_t block,
				   enum gfs2_mark_block mark)
{
	int err = 0;
	err = gfs2_block_clear(il, block, mark);
	if(!err)
		err = gfs2_block_mark(il, block, mark);
	return err;
}

int gfs2_block_clear(struct gfs2_block_list *il, uint64_t block,
					 enum gfs2_mark_block m)
{
	int err = 0;

	switch (m) {
	case gfs2_dup_block:
		err = gfs2_bitmap_clear(&il->list.gbmap.dup_map, block);
		break;
	case gfs2_bad_block:
		err = gfs2_bitmap_clear(&il->list.gbmap.bad_map, block);
		break;
	case gfs2_eattr_block:
		err = gfs2_bitmap_clear(&il->list.gbmap.eattr_map, block);
		break;
	default:
		/* FIXME: check types */
		err = gfs2_bitmap_clear(&il->list.gbmap.group_map, block);
		break;
	}
	return err;
}

int gfs2_block_check(struct gfs2_block_list *il, uint64_t block,
					 struct gfs2_block_query *val)
{
	int err = 0;
	val->bad_block = 0;
	val->dup_block = 0;
	if((err = gfs2_bitmap_get(&il->list.gbmap.group_map, block,
							  &val->block_type)))
		return err;
	if((err = gfs2_bitmap_get(&il->list.gbmap.bad_map, block,
							  &val->bad_block)))
		return err;
	if((err = gfs2_bitmap_get(&il->list.gbmap.dup_map, block,
							  &val->dup_block)))
		return err;
	if((err = gfs2_bitmap_get(&il->list.gbmap.eattr_map, block,
							  &val->eattr_block)))
		return err;
	return err;
}

void *gfs2_block_list_destroy(struct gfs2_block_list *il)
{
	if(il) {
		gfs2_bitmap_destroy(&il->list.gbmap.group_map);
		gfs2_bitmap_destroy(&il->list.gbmap.bad_map);
		gfs2_bitmap_destroy(&il->list.gbmap.dup_map);
		gfs2_bitmap_destroy(&il->list.gbmap.eattr_map);
		free(il);
		il = NULL;
	}
	return il;
}


int gfs2_find_next_block_type(struct gfs2_block_list *il,
							  enum gfs2_mark_block m, uint64_t *b)
{
	uint64_t i;
	uint8_t val;
	int found = 0;
	for(i = *b; ; i++) {
		if(i >= gfs2_bitmap_size(&il->list.gbmap.dup_map))
			return -1;

		switch(m) {
		case gfs2_dup_block:
			if(gfs2_bitmap_get(&il->list.gbmap.dup_map, i, &val))
				return -1;
			if(val)
				found = 1;
			break;
		case gfs2_eattr_block:
			if(gfs2_bitmap_get(&il->list.gbmap.eattr_map, i, &val))
				return -1;
			if(val)
				found = 1;
			break;
		default:
			/* FIXME: add support for getting other types */
			break;
		}
		if(found) {
			*b = i;
			return 0;
		}
	}
	return -1;
}
