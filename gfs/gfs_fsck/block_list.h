#ifndef _BLOCK_LIST_H
#define _BLOCK_LIST_H

#include "bitmap.h"

#define BMAP_COUNT 13

enum block_list_type {
	gbmap = 0,  /* Grouped bitmap */
	dbmap,	    /* Ondisk bitmap - like grouped bitmap, but mmaps
		     * the bitmaps onto file(s) ondisk - not implemented */
};

/* Must be kept in sync with mark_to_bitmap array in block_list.c */
enum mark_block {
	block_free = 0,
	block_used = 1,
	indir_blk = 2,
	inode_dir = 3,
	inode_file = 4,
	inode_lnk = 5,
	inode_blk = 6,
	inode_chr = 7,
	inode_fifo = 8,
	inode_sock = 9,
	leaf_blk = 10,
	journal_blk = 11,
	meta_other = 12,
	meta_free = 13,
	meta_eattr = 14,
	meta_inval = 15,
	/* above this are nibble-values 0x0-0xf */
	bad_block = 16,	/* Contains at least one bad block */
	dup_block = 17,	/* Contains at least one duplicate block */
	eattr_block = 18,	/* Contains an eattr */
};

struct block_query {
	uint8_t block_type;
	uint8_t bad_block;
	uint8_t dup_block;
	uint8_t eattr_block;
};

struct gbmap {
	struct bmap group_map;
	struct bmap bad_map;
	struct bmap dup_map;
	struct bmap eattr_map;
};

struct dbmap {
	struct bmap group_map;
	char *group_file;
	struct bmap bad_map;
	char *bad_file;
	struct bmap dup_map;
	char *dup_file;
	struct bmap eattr_map;
	char *eattr_file;
};

union block_lists {
	struct gbmap gbmap;
	struct dbmap dbmap;
};


/* bitmap implementation */
struct block_list {
	enum block_list_type type;
	/* Union of bitmap, rle */
	union block_lists list;
};


struct block_list *block_list_create(uint64_t size, enum block_list_type type);
int block_mark(struct block_list *il, uint64_t block, enum mark_block mark);
int block_set(struct block_list *il, uint64_t block, enum mark_block mark);
int block_clear(struct block_list *il, uint64_t block, enum mark_block m);
int block_check(struct block_list *il, uint64_t block,
		struct block_query *val);
int block_check_for_mark(struct block_list *il, uint64_t block,
			 enum mark_block mark);
void *block_list_destroy(struct block_list *il);
int find_next_block_type(struct block_list *il, enum mark_block m, uint64_t *b);

#endif /* _BLOCK_LIST_H */
