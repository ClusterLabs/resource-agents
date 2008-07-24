#include <inttypes.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "libgfs2.h"

/* Must be kept in sync with mark_block enum in block_list.h */
/* FIXME: Fragile */
static int mark_to_gbmap[16] = {
	FREE, BLOCK_IN_USE, DIR_INDIR_BLK, DIR_INODE, FILE_INODE,
	LNK_INODE, BLK_INODE, CHR_INODE, FIFO_INODE, SOCK_INODE,
	DIR_LEAF_INODE, JOURNAL_BLK, OTHER_META, EATTR_META,
	INVALID_META, INVALID_META
};

struct gfs2_block_list *gfs2_block_list_create(struct gfs2_sbd *sdp,
					       uint64_t size,
					       uint64_t *addl_mem_needed)
{
	struct gfs2_block_list *il;

	*addl_mem_needed = 0L;
	il = malloc(sizeof(*il));
	if (!il || !memset(il, 0, sizeof(*il)))
		return NULL;

	if(gfs2_bitmap_create(&il->list.gbmap.group_map, size, 4)) {
		*addl_mem_needed = il->list.gbmap.group_map.mapsize;
		free(il);
		il = NULL;
	}
	osi_list_init(&sdp->bad_blocks.list);
	osi_list_init(&sdp->dup_blocks.list);
	osi_list_init(&sdp->eattr_blocks.list);
	return il;
}

void gfs2_special_free(struct special_blocks *blist)
{
	struct special_blocks *f;

	while(!osi_list_empty(&blist->list)) {
		f = osi_list_entry(blist->list.next, struct special_blocks,
				   list);
		osi_list_del(&f->list);
		free(f);
	}
}

struct special_blocks *blockfind(struct special_blocks *blist, uint64_t num)
{
	osi_list_t *head = &blist->list;
	osi_list_t *tmp;
	struct special_blocks *b;

	for (tmp = head->next; tmp != head; tmp = tmp->next) {
		b = osi_list_entry(tmp, struct special_blocks, list);
		if (b->block == num)
			return b;
	}
	return NULL;
}

void gfs2_special_set(struct special_blocks *blocklist, uint64_t block)
{
	struct special_blocks *b;

	if (blockfind(blocklist, block))
		return;
	b = malloc(sizeof(struct special_blocks));
	if (b) {
		b->block = block;
		osi_list_add(&b->list, &blocklist->list);
	}
	return;
}

void gfs2_special_clear(struct special_blocks *blocklist, uint64_t block)
{
	struct special_blocks *b;

	b = blockfind(blocklist, block);
	if (b) {
		osi_list_del(&b->list);
		free(b);
	}
}

int gfs2_block_mark(struct gfs2_sbd *sdp, struct gfs2_block_list *il,
		    uint64_t block, enum gfs2_mark_block mark)
{
	int err = 0;

	if(mark == gfs2_bad_block)
		gfs2_special_set(&sdp->bad_blocks, block);
	else if(mark == gfs2_dup_block)
		gfs2_special_set(&sdp->dup_blocks, block);
	else if(mark == gfs2_eattr_block)
		gfs2_special_set(&sdp->eattr_blocks, block);
	else
		err = gfs2_bitmap_set(&il->list.gbmap.group_map, block,
				      mark_to_gbmap[mark]);
	return err;
}

int gfs2_block_clear(struct gfs2_sbd *sdp, struct gfs2_block_list *il,
		     uint64_t block, enum gfs2_mark_block m)
{
	int err = 0;

	switch (m) {
	case gfs2_dup_block:
		gfs2_special_clear(&sdp->dup_blocks, block);
		break;
	case gfs2_bad_block:
		gfs2_special_clear(&sdp->bad_blocks, block);
		break;
	case gfs2_eattr_block:
		gfs2_special_clear(&sdp->eattr_blocks, block);
		break;
	default:
		/* FIXME: check types */
		err = gfs2_bitmap_clear(&il->list.gbmap.group_map, block);
		break;
	}
	return err;
}

int gfs2_block_set(struct gfs2_sbd *sdp, struct gfs2_block_list *il,
		   uint64_t block, enum gfs2_mark_block mark)
{
	int err;

	err = gfs2_block_clear(sdp, il, block, mark);
	if(!err)
		err = gfs2_block_mark(sdp, il, block, mark);
	return err;
}

int gfs2_block_check(struct gfs2_sbd *sdp, struct gfs2_block_list *il,
		     uint64_t block, struct gfs2_block_query *val)
{
	int err = 0;

	val->bad_block = 0;
	val->dup_block = 0;
	val->eattr_block = 0;
	if((err = gfs2_bitmap_get(&il->list.gbmap.group_map, block,
				  &val->block_type)))
		return err;
	if (blockfind(&sdp->bad_blocks, block))
		val->bad_block = 1;
	if (blockfind(&sdp->dup_blocks, block))
		val->dup_block = 1;
	if (blockfind(&sdp->eattr_blocks, block))
		val->eattr_block = 1;
	return 0;
}

void *gfs2_block_list_destroy(struct gfs2_sbd *sdp, struct gfs2_block_list *il)
{
	if(il) {
		gfs2_bitmap_destroy(&il->list.gbmap.group_map);
		free(il);
		il = NULL;
	}
	gfs2_special_free(&sdp->bad_blocks);
	gfs2_special_free(&sdp->dup_blocks);
	gfs2_special_free(&sdp->eattr_blocks);
	return il;
}
