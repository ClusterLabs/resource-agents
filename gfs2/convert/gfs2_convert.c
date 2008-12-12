/*****************************************************************************
******************************************************************************
**
**  gfs2_convert - convert a gfs1 filesystem into a gfs2 filesystem.
**
******************************************************************************
*****************************************************************************/

#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>
#include <dirent.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "linux_endian.h"
#include <linux/types.h>
#include <linux/gfs2_ondisk.h>
#include "osi_list.h"
#include "copyright.cf"
#include "ondisk.h"
#include "libgfs2.h"
#include "global.h"

/* The following declares are needed because gfs2 can't have  */
/* dependencies on gfs1:                                      */
#define RGRP_STUFFED_BLKS(sb) (((sb)->sb_bsize - sizeof(struct gfs2_rgrp)) * GFS2_NBBY)
#define RGRP_BITMAP_BLKS(sb) (((sb)->sb_bsize - sizeof(struct gfs2_meta_header)) * GFS2_NBBY)

/* Define some gfs1 constants from gfs1's gfs_ondisk.h */
#define GFS_METATYPE_NONE       (0)
#define GFS_METATYPE_SB         (1)    /* Super-Block */
#define GFS_METATYPE_RG         (2)    /* Resource Group Header */
#define GFS_METATYPE_RB         (3)    /* Resource Group Block Alloc BitBlock */
#define GFS_METATYPE_DI         (4)    /* "Disk" inode (dinode) */
#define GFS_METATYPE_IN         (5)    /* Indirect dinode block list */
#define GFS_METATYPE_LF         (6)    /* Leaf dinode block list */
#define GFS_METATYPE_JD         (7)    /* Journal Data */
#define GFS_METATYPE_LH         (8)    /* Log Header (gfs_log_header) */
#define GFS_METATYPE_LD         (9)    /* Log Descriptor (gfs_log_descriptor) */
#define GFS_METATYPE_EA         (10)   /* Extended Attribute */
#define GFS_METATYPE_ED         (11)   /* Extended Attribute data */

/* GFS1 Dinode types  */
#define GFS_FILE_NON            (0)
#define GFS_FILE_REG            (1)    /* regular file */
#define GFS_FILE_DIR            (2)    /* directory */
#define GFS_FILE_LNK            (5)    /* link */
#define GFS_FILE_BLK            (7)    /* block device node */
#define GFS_FILE_CHR            (8)    /* character device node */
#define GFS_FILE_FIFO           (101)  /* fifo/pipe */
#define GFS_FILE_SOCK           (102)  /* socket */

#define GFS_FORMAT_SB           (100)  /* Super-Block */
#define GFS_FORMAT_FS           (1309) /* Filesystem (all-encompassing) */
#define GFS_FORMAT_MULTI        (1401) /* Multi-Host */

struct gfs1_rgrp {
	struct gfs2_meta_header rg_header; /* hasn't changed from gfs1 to 2 */
	uint32_t rg_flags;
	uint32_t rg_free;       /* Number (qty) of free data blocks */
	/* Dinodes are USEDMETA, but are handled separately from other METAs */
	uint32_t rg_useddi;     /* Number (qty) of dinodes (used or free) */
	uint32_t rg_freedi;     /* Number (qty) of unused (free) dinodes */
	struct gfs2_inum rg_freedi_list; /* hasn't changed from gfs1 to 2 */
	/* These META statistics do not include dinodes (used or free) */
	uint32_t rg_usedmeta;   /* Number (qty) of used metadata blocks */
	uint32_t rg_freemeta;   /* Number (qty) of unused metadata blocks */
	char rg_reserved[64];
};

struct gfs1_jindex {
	uint64_t ji_addr;       /* starting block of the journal */
	uint32_t ji_nsegment;   /* number (quantity) of segments in journal */
	uint32_t ji_pad;

	char ji_reserved[64];
};

struct gfs1_sb {
	/*  Order is important; need to be able to read old superblocks
		in order to support on-disk version upgrades */
	struct gfs2_meta_header sb_header;

	uint32_t sb_fs_format;         /* GFS_FORMAT_FS (on-disk version) */
	uint32_t sb_multihost_format;  /* GFS_FORMAT_MULTI */
	uint32_t sb_flags;             /* ?? */

	uint32_t sb_bsize;             /* fundamental FS block size in bytes */
	uint32_t sb_bsize_shift;       /* log2(sb_bsize) */
	uint32_t sb_seg_size;          /* Journal segment size in FS blocks */

	/* These special inodes do not appear in any on-disk directory. */
	struct gfs2_inum sb_jindex_di;  /* journal index inode */
	struct gfs2_inum sb_rindex_di;  /* resource group index inode */
	struct gfs2_inum sb_root_di;    /* root directory inode */
	
	/* Default inter-node locking protocol (lock module) and namespace */
	char sb_lockproto[GFS2_LOCKNAME_LEN]; /* lock protocol name */
	char sb_locktable[GFS2_LOCKNAME_LEN]; /* unique name for this FS */
	
	/* More special inodes */
	struct gfs2_inum sb_quota_di;   /* quota inode */
	struct gfs2_inum sb_license_di; /* license inode */

	char sb_reserved[96];
};

struct inode_block {
	osi_list_t list;
	uint64_t di_addr;
};

struct blocklist {
	osi_list_t list;
	uint64_t lbparent; /* parent's buffer offset */
	uint64_t dbparent; /* parent's disk block */
	uint64_t dblock; /* disk block */
	uint64_t lblock; /* logical block relative to start of file */
};

struct metapath2 {
	unsigned int mp_list[GFS2_MAX_META_HEIGHT];
	struct gfs2_buffer_head *mp_bh[GFS2_MAX_META_HEIGHT];
};

struct gfs1_sb  raw_gfs1_ondisk_sb;
struct gfs2_sbd sb2;
char device[256];
struct inode_block dirs_to_fix;  /* linked list of directories to fix */
int seconds;
struct timeval tv;
uint64_t dirs_fixed;
uint64_t dirents_fixed;
char *prog_name = "gfs2_convert"; /* needed by libgfs2 */
struct gfs1_jindex *sd_jindex = NULL;    /* gfs1 journal index in memory */
int gfs2_inptrs;
uint64_t gfs2_heightsize[GFS2_MAX_META_HEIGHT];
uint64_t gfs2_jheightsize[GFS2_MAX_META_HEIGHT];
int gfs2_max_height;
int gfs2_max_jheight;

/* ------------------------------------------------------------------------- */
/* This function is for libgfs's sake.                                       */
/* ------------------------------------------------------------------------- */
void print_it(const char *label, const char *fmt, const char *fmt2, ...)
{
	va_list args;

	va_start(args, fmt2);
	printf("%s: ", label);
	vprintf(fmt, args);
	va_end(args);
}

/* ------------------------------------------------------------------------- */
/* convert_bitmaps - Convert gfs1 bitmaps to gfs2 bitmaps.                   */
/*                   Fixes all unallocated metadata bitmap states (which are */
/*                   valid in gfs1 but invalid in gfs2).                     */
/* ------------------------------------------------------------------------- */
void convert_bitmaps(struct gfs2_sbd *sdp, struct rgrp_list *rgd2,
					 int read_disk)
{
	uint32_t blk;
	int x, y;
	struct gfs2_rindex *ri;
	unsigned char state;
	struct gfs2_buffer_head *bh;

	ri = &rgd2->ri;
	if (gfs2_compute_bitstructs(sdp, rgd2)) { /* mallocs bh as array */
		log_crit("gfs2_convert: Error converting bitmaps.\n");
		exit(-1);
	}
	for (blk = 0; blk < ri->ri_length; blk++) {
		bh = bget_generic(&sdp->nvbuf_list, ri->ri_addr + blk,
				  read_disk, read_disk);
		if (!rgd2->bh[blk])
			rgd2->bh[blk] = bh;
		x = (blk) ? sizeof(struct gfs2_meta_header) : sizeof(struct gfs2_rgrp);

		for (; x < sdp->bsize; x++)
			for (y = 0; y < GFS2_NBBY; y++) {
				state = (rgd2->bh[blk]->b_data[x] >>
						 (GFS2_BIT_SIZE * y)) & 0x03;
				if (state == 0x02) /* unallocated metadata state invalid */
					rgd2->bh[blk]->b_data[x] &= ~(0x02 << (GFS2_BIT_SIZE * y));
			}
		brelse(bh, updated);
	}
}/* convert_bitmaps */

/* ------------------------------------------------------------------------- */
/* convert_rgs - Convert gfs1 resource groups to gfs2.                       */
/* Returns: 0 on success, -1 on failure                                      */
/* ------------------------------------------------------------------------- */
static int convert_rgs(struct gfs2_sbd *sbp)
{
	struct rgrp_list *rgd;
	osi_list_t *tmp;
	struct gfs2_buffer_head *bh;
	struct gfs1_rgrp *rgd1;
	int rgs = 0;

	/* --------------------------------- */
	/* Now convert its rgs into gfs2 rgs */
	/* --------------------------------- */
	osi_list_foreach(tmp, &sbp->rglist) {
		rgd = osi_list_entry(tmp, struct rgrp_list, list);
		rgd1 = (struct gfs1_rgrp *)&rgd->rg; /* recast as gfs1 structure */
		/* rg_freemeta is a gfs1 structure, so libgfs2 doesn't know to */
		/* convert from be to cpu. We must do it now. */
		rgd->rg.rg_free = rgd1->rg_free + be32_to_cpu(rgd1->rg_freemeta);

		sbp->blks_total += rgd->ri.ri_data;
		sbp->blks_alloced += (rgd->ri.ri_data - rgd->rg.rg_free);
		sbp->dinodes_alloced += rgd1->rg_useddi;
		convert_bitmaps(sbp, rgd, TRUE);
		/* Write the updated rgrp to the gfs2 buffer */
		bh = bget(&sbp->nvbuf_list,
			  rgd->ri.ri_addr); /* get a gfs2 buffer for the rg */
		gfs2_rgrp_out(&rgd->rg, rgd->bh[0]->b_data);
		brelse(bh, updated); /* release the buffer */
		rgs++;
		if (rgs % 100 == 0) {
			printf(".");
			fflush(stdout);
		}
	}
	return 0;
}/* superblock_cvt */

/* ------------------------------------------------------------------------- */
/* find_lbh - find a gfs2_buffer_head in a list, based on relative block #   */
/* ------------------------------------------------------------------------- */
struct gfs2_buffer_head *find_lbh(struct gfs2_sbd *sbp,
				  struct blocklist *blist, uint64_t lblock)
{
	struct gfs2_buffer_head *bh;
	osi_list_t *head, *tmp;
	struct blocklist *blk;

	head = &blist->list;

	for (tmp = head->next; tmp != head; tmp = tmp->next) {
		blk = osi_list_entry(tmp, struct blocklist, list);
		if (blk->lbparent == lblock) {
			bh = bget(&sbp->buf_list, blk->dblock);
			return bh;
		}
	}
	/* We didn't find one that has the same offset, so let's just reuse
	   a bh from the same height */
	blk->lbparent = lblock;
	bh = bget(&sbp->buf_list, blk->dblock);
	return bh;
}

/* ------------------------------------------------------------------------- */
/* calc_gfs2_tree_height - calculate new dinode height as if this is gfs2    */
/* ------------------------------------------------------------------------- */
unsigned int calc_gfs2_tree_height(struct gfs2_inode *ip, uint64_t size)
{
	uint64_t *arr;
	unsigned int max, height;

	if (ip->i_di.di_size > size)
		size = ip->i_di.di_size;

	if (S_ISDIR(ip->i_di.di_mode)) {
		arr = gfs2_jheightsize;
		max = gfs2_max_jheight;
	} else {
		arr = gfs2_heightsize;
		max = gfs2_max_height;
	}

	for (height = 0; height < max; height++)
		if (arr[height] >= size)
			break;

	return height;
}

/* ------------------------------------------------------------------------- */
/* gfs1_mp_to_lblock - convert a gfs1 metapath back to a logical block num.  */
/* ------------------------------------------------------------------------- */
uint64_t gfs1_mp_to_lblock(struct gfs2_sbd *sbp, int height, int width,
			   struct blocklist *srcblk,
			   struct blocklist *blocks[GFS2_MAX_META_HEIGHT])
{
	uint64_t lblock;
	osi_list_t *head, *tmp;
	int h;
	struct blocklist *blk, *higher_blk = srcblk;
	uint64_t gfs1factor[GFS2_MAX_META_HEIGHT];

	/* figure out multiplication factors for each height */
	memset(&gfs1factor, 0, sizeof(gfs1factor));
	gfs1factor[height] = 1ull;
	for (h = height; h > 0; h--)
		gfs1factor[h - 1] = gfs1factor[h] * sbp->sd_inptrs;
	/* First, factor in our own height */
	lblock = width;
	for (h = height - 1; h > 0; h--) {
		head = &blocks[h]->list;
		for (tmp = head->next; tmp != head; tmp = tmp->next) {
			blk = osi_list_entry(tmp, struct blocklist, list);
			if (higher_blk->dbparent == blk->dblock) {
				lblock += (blk->lbparent * gfs1factor[h]);
				higher_blk = blk;
				break; /* found it, go to the next height */
			}
		}
	}
	return lblock;
}

/* ------------------------------------------------------------------------- */
/* adjust_indirect_blocks - convert all gfs_indirect blocks to gfs2.         */
/*                                                                           */
/* This function converts all gfs_indirect blocks to GFS2.  The difference   */
/* is that gfs1 indirect block has a 64-byte chunk of reserved space that    */
/* gfs2 does not.  Since GFS block locations (relative to the start of the   */
/* file have their locations defined by the offset from the end of the       */
/* structure, all block pointers must be shifted.                            */
/*                                                                           */
/* Stuffed inodes don't need to be shifted at since there are no indirect    */
/* blocks.  Inodes with height 1 don't need to be shifted either, because    */
/* the dinode size is the same between gfs and gfs2 (232 bytes), and         */
/* therefore you can fit the same number of block pointers after the dinode  */
/* structure.  For the normal 4K block size, that's 483 pointers.  For 1K    */
/* blocks, it's 99 pointers.                                                 */
/*                                                                           */
/* At height 2 things get complex.  GFS1 reserves an area of 64 (0x40) bytes */
/* at the start of the indirect block, so for 4K blocks, you can fit 501     */
/* pointers.  GFS2 doesn't reserve that space, so you can fit 509 pointers.  */
/* For 1K blocks, it's 117 pointers in GFS1 and 125 in GFS2.                 */
/*                                                                           */
/* That means, for example, that if you have 4K blocks, a 946MB file will    */
/* require a height of 3 for GFS, but only a height of 2 for GFS2.           */
/* There isn't a good way to shift the pointers around from one height to    */
/* another, so the only way to do it is to rebuild all those indirect blocks */
/* from empty ones.                                                          */
/*                                                                           */
/* For example, with a 1K block size, if you do:                             */
/*                                                                           */
/* dd if=/mnt/gfs/big of=/tmp/tocompare skip=496572346368 bs=1024 count=1    */
/*                                                                           */
/* the resulting metadata paths will look vastly different for the data:     */
/*                                                                           */
/* height    0     1     2     3     4     5                                 */
/* GFS1:  0x16  0x46  0x70  0x11  0x5e  0x4a                                 */
/* GFS2:  0x10  0x21  0x78  0x05  0x14  0x78                                 */
/*                                                                           */
/* To complicate matters, we can't really require free space.  A user might  */
/* be trying to migrate a "full" gfs1 file system to GFS2.  After we         */
/* convert the journals to GFS2, we might have more free space, so we can    */
/* allocate blocks at that time.                                             */
/*                                                                           */
/* Assumes: GFS1 values are in place for diptrs and inptrs.                  */
/*                                                                           */
/* Returns: 0 on success, -1 on failure                                      */
/*                                                                           */
/* Adapted from gfs2_fsck metawalk.c's build_and_check_metalist              */
/* ------------------------------------------------------------------------- */
int adjust_indirect_blocks(struct gfs2_sbd *sbp, struct gfs2_buffer_head *dibh,
			   struct gfs2_inode *ip)
{
	osi_list_t metalist[GFS2_MAX_META_HEIGHT];
	uint32_t height1 = ip->i_di.di_height, gfs2_height;
	struct gfs2_buffer_head *bh, *nbh;
	osi_list_t *next_list, *cur_list, *tmp, *head;
	int h, h2, head_size1, head_size2;
	uint64_t *ptr1, *ptr2, block, b, metablocks_gfs1, metablocks_gfs2;
	int error = 0;
	struct metapath mp;
	struct blocklist *blocks[GFS2_MAX_META_HEIGHT], *blk;

	/* if there are no indirect blocks to check */
	if (height1 <= 1)
		return 0;

	/* zero all pointers so we can tell what to free if we need to abort */
	for (h = 0; h <= height1; h++)
		blocks[h] = NULL;
	for (h = 0; h <= height1; h++) {
		blocks[h] = malloc(sizeof(struct blocklist));
		if (!blocks[h]) {
			log_crit("Error: Can't allocate memory for indirect "
				 "block adjustment\n");
			error = -1;
			goto out;
		}
		memset(blocks[h], 0, sizeof(struct blocklist));
		osi_list_init(&blocks[h]->list);
	}

	/* Now run the metadata chain and build the block lists */
	for (h = 0; h < GFS2_MAX_META_HEIGHT; h++)
		osi_list_init(&metalist[h]);

	osi_list_add(&dibh->b_altlist, &metalist[0]);
	memset(&mp, 0, sizeof(mp));

	/* Add the dinode block to the metaheader list */
	blk = malloc(sizeof(struct blocklist));
	if (!blk) {
		log_crit("Error: Can't allocate memory"
			 " for indirect block fix.\n");
		error = -1;
		goto out;
	}
	memset(blk, 0, sizeof(*blk));
	blk->dbparent = 0ull;
	blk->dblock = ip->i_di.di_num.no_addr;
	blk->lbparent = -1;
	blk->lblock = 0ull;
	osi_list_add_prev(&blk->list, &blocks[0]->list);

	for (h = 0; h < height1; h++){
		cur_list = &metalist[h];
		next_list = &metalist[h + 1]; /* future metadata to process */
		head_size1 = (h > 0 ? sizeof(struct gfs_indirect) :
			      sizeof(struct gfs_dinode));

		mp.mp_list[h] = 0;
		for (tmp = cur_list->next; tmp != cur_list; tmp = tmp->next){
			bh = osi_list_entry(tmp, struct gfs2_buffer_head,
					    b_altlist);

			for (ptr1 = (uint64_t *)(bh->b_data + head_size1);
			     (char *)ptr1 < (bh->b_data + sbp->bsize);
			     ptr1++, mp.mp_list[h]++) {
				nbh = NULL;
				if (!*ptr1)
					continue;

				b = block = be64_to_cpu(*ptr1);

				blk = malloc(sizeof(struct blocklist));
				if (!blk) {
					log_crit("Error: Can't allocate memory"
					       " for indirect block fix.\n");
					error = -1;
					goto out;
				}
				memset(blk, 0, sizeof(*blk));
				blk->dbparent = bh->b_blocknr;
				blk->dblock = block;
				blk->lbparent = mp.mp_list[h];
				osi_list_add_prev(&blk->list,
						  &blocks[h + 1]->list);
				if (h == height1 - 1)
					blk->lblock = gfs1_mp_to_lblock(sbp,
								height1,
								mp.mp_list[h],
								blk, blocks);
				else
					blk->lblock = 0ull;
				if (h == height1 - 1) /* if not metadata */
					continue; /* don't queue it up */
				/* read the next metadata block in the chain */
				if(!nbh)
					nbh = bread(&sbp->buf_list, block);
				osi_list_add_prev(&nbh->b_altlist, next_list);
			} /* for all data on the indirect block */
			for (h2 = h; h2 > 0; h2--)
				if (mp.mp_list[h2] >= sbp->sd_inptrs) {
					mp.mp_list[h2 - 1]++;
					mp.mp_list[h2] = 0;
				}
		} /* for blocks at that height */
	} /* for height */

	/* Run through the metadata block list and zero out all the block */
	/* pointers in the buffers so we can start clean.                 */
	/* We've got to keep track of how many blocks we actually use for */
	/* the converted metadata because we need to free the blocks we   */
	/* don't use and mark the proper number as "blocks used" in the   */
	/* dinode's di_blocks.  For example, in a normal 4K block file    */
	/* system a contiguous file that is 4152273 bytes long needs 1014 */
	/* data blocks.  GFS1 needs 3 gfs_indirect metadata blocks to     */
	/* keep track: (1) 0-500, (2) 501-1001, (3) 1002=1014.  However,  */
	/* GFS2 only needs 2 metadata blocks to keep track: (1) 0-508,    */
	/* (2) 509-1014.  So the dinode's count of blocks used has to be  */
	/* one less, and that final block needs to be freed.              */
	/* I'm going to use the bh->b_changed flag to keep track of which */
	/* blocks we actually need and which ones we don't.               */
	metablocks_gfs1 = 0;

	for (h = 0; h < height1; h++) {
		head = &blocks[h]->list;
		for (tmp = head->next; tmp != head; tmp = tmp->next) {
			blk = osi_list_entry(tmp, struct blocklist, list);
			bh = bget(&sbp->buf_list, blk->dblock);
			head_size2 = sizeof(struct gfs2_meta_header);
			memset(bh->b_data + head_size2, 0,
			       sbp->bsize - head_size2);
			bh->b_changed = not_updated; /* not used--yet */
			metablocks_gfs1++;
			brelse(bh, not_updated); /* not used--yet */
		}
	}

	/* zero the dinode's pointers too */
	head_size2 = sizeof(struct gfs2_dinode);
	memset(dibh->b_data + sizeof(struct gfs2_dinode), 0,
	       sbp->bsize - head_size2);

	/* The gfs2 height may be different */
	gfs2_height = calc_gfs2_tree_height(ip, ip->i_di.di_size);

	/* Now run through the data block list and reformat the blocks.      */
	/* We want to reuse the same metadata blocks from the list we built. */
	head = &blocks[gfs2_height]->list; /* highest height:data blocks */

	for (tmp = head->next; tmp != head; tmp = tmp->next) {
		struct metapath2 mp2; /* metapath in gfs2 terms */
		struct metapath *mp1;  /* metapath in gfs1 terms */

		blk = osi_list_entry(tmp, struct blocklist, list);
		block = blk->dblock;

		/* recalculate the metapath */
		mp1 = find_metapath(ip, blk->lblock);
		/* figure out the block number from the start of the file. */
		b = blk->lblock;
		/* Calculate the metapath in GFS2 terms */
		memset(&mp2, 0, sizeof(mp2));
		for (h = gfs2_height; h--;)
			mp2.mp_list[h] = do_div(b, gfs2_inptrs);
		for (h = gfs2_height - 1; h >= 0; h--) {
			if (h) {
				/* find/reuse a bh from the old metadata. */
				mp2.mp_bh[h] = find_lbh(sbp, blocks[h],
							mp2.mp_list[h - 1]);
			} else {
				mp2.mp_bh[h] = dibh;
			}
			head_size2 = (h > 0 ? sizeof(struct gfs2_meta_header) :
				      sizeof(struct gfs2_dinode));
			ptr2 = (uint64_t *)(mp2.mp_bh[h]->b_data + head_size2);
			ptr2 += mp2.mp_list[h];
			*ptr2 = cpu_to_be64(block);
			block = mp2.mp_bh[h]->b_blocknr;
			if (h)
				brelse(mp2.mp_bh[h], updated); /* used */
		}
		free(mp1);
	} /* for all data blocks */

	/* Release all the buffers we were using. */
	/* We need to count the number of blocks we really used in the new */
	/* metadata along the way. */
	metablocks_gfs2 = 1; /* 1 for the inode that we know will change */
	for (h = 1; h < height1; h++){
		cur_list = &metalist[h];
		for (tmp = cur_list->next; tmp != cur_list; tmp = tmp->next){
			bh = osi_list_entry(tmp, struct gfs2_buffer_head,
					    b_altlist);
			if (bh->b_changed)     /* if we changed the block */
				metablocks_gfs2++; /* count it */
			else                   /* if we didn't change it */
				gfs2_free_block(sbp, bh->b_blocknr); /* free */
			brelse(bh, updated);
		}
	}
	/* Set the new dinode height, which may or may not have changed.  */
	/* The caller will take it from the ip and write it to the buffer */
	ip->i_di.di_height = gfs2_height;
	ip->i_di.di_blocks -= (metablocks_gfs1 - metablocks_gfs2);

out:
	for (h = 0; h <= height1; h++) {
		head = &blocks[h]->list;
		tmp = head->next;
		while (tmp != head) {
			blk = osi_list_entry(tmp, struct blocklist, list);
			tmp = tmp->next;
			free(blk);
		}
		if (blocks[h])
			free(blocks[h]);
	}
	return error;
}

/* ------------------------------------------------------------------------- */
/* adjust_inode - change an inode from gfs1 to gfs2                          */
/*                                                                           */
/* Returns: 0 on success, -1 on failure                                      */
/* ------------------------------------------------------------------------- */
int adjust_inode(struct gfs2_sbd *sbp, struct gfs2_buffer_head *bh)
{
	struct gfs2_inode *inode;
	struct inode_block *fixdir;
	int inode_was_gfs1;

	inode = gfs_inode_get(sbp, bh);

	inode_was_gfs1 = (inode->i_di.di_num.no_formal_ino ==
					  inode->i_di.di_num.no_addr);
	/* Fix the inode number: */
	inode->i_di.di_num.no_formal_ino = sbp->md.next_inum;           ;
	
	/* Fix the inode type: gfs1 uses di_type, gfs2 uses di_mode. */
	switch (inode->i_di.__pad1) { /* formerly di_type */
	case GFS_FILE_DIR:           /* directory        */
		inode->i_di.di_mode |= S_IFDIR;
		/* Add this directory to the list of dirs to fix later. */
		fixdir = malloc(sizeof(struct inode_block));
		if (!fixdir) {
			log_crit("Error: out of memory.\n");
			return -1;
		}
		memset(fixdir, 0, sizeof(struct inode_block));
		fixdir->di_addr = inode->i_di.di_num.no_addr;
		osi_list_add_prev((osi_list_t *)&fixdir->list,
						  (osi_list_t *)&dirs_to_fix);
		break;
	case GFS_FILE_REG:           /* regular file     */
		inode->i_di.di_mode |= S_IFREG;
		break;
	case GFS_FILE_LNK:           /* symlink          */
		inode->i_di.di_mode |= S_IFLNK;
		break;
	case GFS_FILE_BLK:           /* block device     */
		inode->i_di.di_mode |= S_IFBLK;
		break;
	case GFS_FILE_CHR:           /* character device */
		inode->i_di.di_mode |= S_IFCHR;
		break;
	case GFS_FILE_FIFO:          /* fifo / pipe      */
		inode->i_di.di_mode |= S_IFIFO;
		break;
	case GFS_FILE_SOCK:          /* socket           */
		inode->i_di.di_mode |= S_IFSOCK;
		break;
	}
			
	/* ----------------------------------------------------------- */
	/* gfs2 inodes are slightly different from gfs1 inodes in that */
	/* di_goal_meta has shifted locations and di_goal_data has     */
	/* changed from 32-bits to 64-bits.  The following code        */
	/* adjusts for the shift.                                      */
	/*                                                             */
	/* Note: It may sound absurd, but we need to check if this     */
	/*       inode has already been converted to gfs2 or if it's   */
	/*       still a gfs1 inode.  That's just in case there was a  */
	/*       prior attempt to run gfs2_convert that never finished */
	/*       (due to power out, ctrl-c, kill, segfault, whatever.) */
	/*       If it is unconverted gfs1 we want to do a full        */
	/*       conversion.  If it's a gfs2 inode from a prior run,   */
	/*       we still need to renumber the inode, but here we      */
	/*       don't want to shift the data around.                  */
	/* ----------------------------------------------------------- */
	if (inode_was_gfs1) {
		struct gfs_dinode *gfs1_dinode_struct;

		gfs1_dinode_struct = (struct gfs_dinode *)&inode->i_di;
		inode->i_di.di_goal_meta = inode->i_di.di_goal_data;
		inode->i_di.di_goal_data = 0; /* make sure the upper 32b are 0 */
		inode->i_di.di_goal_data = gfs1_dinode_struct->di_goal_dblk;
		inode->i_di.di_generation = 0;
		if (adjust_indirect_blocks(sbp, bh, inode))
			return -1;
	}
	
	gfs2_dinode_out(&inode->i_di, bh->b_data);
	sbp->md.next_inum++; /* update inode count */
	return 0;
} /* adjust_inode */

/* ------------------------------------------------------------------------- */
/* inode_renumber - renumber the inodes                                      */
/*                                                                           */
/* In gfs1, the inode number WAS the inode address.  In gfs2, the inodes are */
/* numbered sequentially.                                                    */
/*                                                                           */
/* Returns: 0 on success, -1 on failure                                      */
/* ------------------------------------------------------------------------- */
int inode_renumber(struct gfs2_sbd *sbp, uint64_t root_inode_addr)
{
	struct rgrp_list *rgd;
	osi_list_t *tmp;
	uint64_t block;
	struct gfs2_buffer_head *bh;
	int first;
	int error = 0;
	int rgs_processed = 0;

	log_notice("Converting inodes.\n");
	sbp->md.next_inum = 1; /* starting inode numbering */
	gettimeofday(&tv, NULL);
	seconds = tv.tv_sec;

	/* ---------------------------------------------------------------- */
	/* Traverse the resource groups to figure out where the inodes are. */
	/* ---------------------------------------------------------------- */
	osi_list_foreach(tmp, &sbp->rglist) {
		rgs_processed++;
		rgd = osi_list_entry(tmp, struct rgrp_list, list);
		first = 1;
		if (gfs2_rgrp_read(sbp, rgd)) {
			log_crit("Unable to read rgrp.\n");
			return -1;
		}
		while (1) {    /* for all inodes in the resource group */
			gettimeofday(&tv, NULL);
			/* Put out a warm, fuzzy message every second so the customer */
			/* doesn't think we hung.  (This may take a long time).       */
			if (tv.tv_sec - seconds) {
				seconds = tv.tv_sec;
				log_notice("\r%" PRIu64" inodes from %d rgs "
					   "converted.", sbp->md.next_inum,
					   rgs_processed);
				fflush(stdout);
			}
			/* Get the next metadata block.  Break out if we reach the end. */
            /* We have to check all metadata blocks because the bitmap may  */
			/* be "11" (used meta) for both inodes and indirect blocks.     */
			/* We need to process the inodes and change the indirect blocks */
			/* to have a bitmap type of "01" (data).                        */
			if (gfs2_next_rg_metatype(sbp, rgd, &block, 0, first))
				break;
			/* If this is the root inode block, remember it for later: */
			if (block == root_inode_addr) {
				sbp->sd_sb.sb_root_dir.no_addr = block;
				sbp->sd_sb.sb_root_dir.no_formal_ino = sbp->md.next_inum;
			}
			bh = bread(&sbp->buf_list, block);
			if (!gfs2_check_meta(bh, GFS_METATYPE_DI)) /* if it is an dinode */
				error = adjust_inode(sbp, bh);
			else { /* It's metadata, but not an inode, so fix the bitmap. */
				int blk, buf_offset;
				int bitmap_byte; /* byte within the bitmap to fix */
				int byte_bit; /* bit within the byte */

				/* Figure out the absolute bitmap byte we need to fix.   */
				/* ignoring structure offsets and bitmap blocks for now. */
				bitmap_byte = (block - rgd->ri.ri_data0) / GFS2_NBBY;
				byte_bit = (block - rgd->ri.ri_data0) % GFS2_NBBY;
				/* Now figure out which bitmap block the byte is on */
				for (blk = 0; blk < rgd->ri.ri_length; blk++) {
                    /* figure out offset of first bitmap byte for this map: */
					buf_offset = (blk) ? sizeof(struct gfs2_meta_header) :
						sizeof(struct gfs2_rgrp);
					/* if it's on this page */
					if (buf_offset + bitmap_byte < sbp->bsize) {
						rgd->bh[blk]->b_data[buf_offset + bitmap_byte] &=
							~(0x03 << (GFS2_BIT_SIZE * byte_bit));
						rgd->bh[blk]->b_data[buf_offset + bitmap_byte] |=
							(0x01 << (GFS2_BIT_SIZE * byte_bit));
						break;
					}
					bitmap_byte -= (sbp->bsize - buf_offset);
				}
			}
			brelse(bh, updated);
			first = 0;
		} /* while 1 */
		gfs2_rgrp_relse(rgd, updated);
	} /* for all rgs */
	log_notice("\r%" PRIu64" inodes from %d rgs converted.",
		   sbp->md.next_inum, rgs_processed);
	fflush(stdout);
	return 0;
}/* inode_renumber */

/* ------------------------------------------------------------------------- */
/* fetch_inum - fetch an inum entry from disk, given its block               */
/* ------------------------------------------------------------------------- */
int fetch_inum(struct gfs2_sbd *sbp, uint64_t iblock,
					   struct gfs2_inum *inum)
{
	struct gfs2_buffer_head *bh_fix;
	struct gfs2_inode *fix_inode;

	bh_fix = bread(&sbp->buf_list, iblock);
	fix_inode = inode_get(sbp, bh_fix);
	inum->no_formal_ino = fix_inode->i_di.di_num.no_formal_ino;
	inum->no_addr = fix_inode->i_di.di_num.no_addr;
	brelse(bh_fix, updated);
	return 0;
}/* fetch_inum */

/* ------------------------------------------------------------------------- */
/* process_dirent_info - fix one dirent (directory entry) buffer             */
/*                                                                           */
/* We changed inode numbers, so we must update that number into the          */
/* directory entries themselves.                                             */
/*                                                                           */
/* Returns: 0 on success, -1 on failure                                      */
/* ------------------------------------------------------------------------- */
int process_dirent_info(struct gfs2_inode *dip, struct gfs2_sbd *sbp,
						struct gfs2_buffer_head *bh, int dir_entries)
{
	int error;
	struct gfs2_dirent *dent;
	int de; /* directory entry index */
	
	error = gfs2_dirent_first(dip, bh, &dent);
	if (error != IS_LEAF && error != IS_DINODE) {
		log_crit("Error retrieving directory.\n");
		return -1;
	}
	/* Go through every dirent in the buffer and process it. */
	/* Turns out you can't trust dir_entries is correct.     */
	for (de = 0; ; de++) {
		struct gfs2_inum inum;
		int dent_was_gfs1;
		
		gettimeofday(&tv, NULL);
		/* Do more warm fuzzy stuff for the customer. */
		dirents_fixed++;
		if (tv.tv_sec - seconds) {
			seconds = tv.tv_sec;
			log_notice("\r%" PRIu64 " directories, %" PRIu64 " dirents fixed.",
					   dirs_fixed, dirents_fixed);
			fflush(stdout);
		}
		/* fix the dirent's inode number based on the inode */
		gfs2_inum_in(&inum, (char *)&dent->de_inum);
		dent_was_gfs1 = (dent->de_inum.no_addr == dent->de_inum.no_formal_ino);
		if (inum.no_formal_ino) { /* if not a sentinel (placeholder) */
			error = fetch_inum(sbp, inum.no_addr, &inum);
			if (error) {
				log_crit("Error retrieving inode %" PRIx64 "\n", inum.no_addr);
				break;
			}
			/* fix the dirent's inode number from the fetched inum. */
			dent->de_inum.no_formal_ino = cpu_to_be64(inum.no_formal_ino);
		}
		/* Fix the dirent's filename hash: They are the same as gfs1 */
		/* dent->de_hash = cpu_to_be32(gfs2_disk_hash((char *)(dent + 1), */
		/*                             be16_to_cpu(dent->de_name_len))); */
		/* Fix the dirent's file type.  Gfs1 used home-grown values.  */
		/* Gfs2 uses standard values from include/linux/fs.h          */
		/* Only do this if the dent was a true gfs1 dent, and not a   */
		/* gfs2 dent converted from a previously aborted run.         */
		if (dent_was_gfs1) {
			switch be16_to_cpu(dent->de_type) {
			case GFS_FILE_NON:
				dent->de_type = cpu_to_be16(DT_UNKNOWN);
				break;
			case GFS_FILE_REG:    /* regular file */
				dent->de_type = cpu_to_be16(DT_REG);
				break;
			case GFS_FILE_DIR:    /* directory */
				dent->de_type = cpu_to_be16(DT_DIR);
				break;
			case GFS_FILE_LNK:    /* link */
				dent->de_type = cpu_to_be16(DT_LNK);
				break;
			case GFS_FILE_BLK:    /* block device node */
				dent->de_type = cpu_to_be16(DT_BLK);
				break;
			case GFS_FILE_CHR:    /* character device node */
				dent->de_type = cpu_to_be16(DT_CHR);
				break;
			case GFS_FILE_FIFO:   /* fifo/pipe */
				dent->de_type = cpu_to_be16(DT_FIFO);
				break;
			case GFS_FILE_SOCK:   /* socket */
				dent->de_type = cpu_to_be16(DT_SOCK);
				break;
			}
		}
		error = gfs2_dirent_next(dip, bh, &dent);
		if (error)
			break;
	} /* for every directory entry */
	return 0;
}/* process_dirent_info */

/* ------------------------------------------------------------------------- */
/* fix_one_directory_exhash - fix one directory's inode numbers.             */
/*                                                                           */
/* This is for exhash directories, where the inode has a list of "leaf"      */
/* blocks, each of which is a buffer full of dirents that must be processed. */
/*                                                                           */
/* Returns: 0 on success, -1 on failure                                      */
/* ------------------------------------------------------------------------- */
int fix_one_directory_exhash(struct gfs2_sbd *sbp, struct gfs2_inode *dip)
{
	struct gfs2_buffer_head *bh_leaf;
	int error;
	uint64_t leaf_block, prev_leaf_block;
	uint32_t leaf_num;
	
	prev_leaf_block = 0;
	/* for all the leafs, get the leaf block and process the dirents inside */
	for (leaf_num = 0; ; leaf_num++) {
		uint64 buf;
		struct gfs2_leaf leaf;

		error = gfs2_readi(dip, (char *)&buf, leaf_num * sizeof(uint64),
						   sizeof(uint64));
		if (!error) /* end of file */
			return 0; /* success */
		else if (error != sizeof(uint64)) {
			log_crit("fix_one_directory_exhash: error reading directory.\n");
			return -1;
		}
		else {
			leaf_block = be64_to_cpu(buf);
			error = 0;
		}
		/* leaf blocks may be repeated, so skip the duplicates: */
		if (leaf_block == prev_leaf_block) /* same block? */
			continue;                      /* already converted */
		prev_leaf_block = leaf_block;
		/* read the leaf buffer in */
		error = gfs2_get_leaf(dip, leaf_block, &bh_leaf);
		if (error) {
			log_crit("Error reading leaf %" PRIx64 "\n", leaf_block);
			break;
		}
		gfs2_leaf_in(&leaf, (char *)bh_leaf->b_data); /* buffer to structure */
		error = process_dirent_info(dip, sbp, bh_leaf, leaf.lf_entries);
		brelse(bh_leaf, updated);
	} /* for leaf_num */
	return 0;
}/* fix_one_directory_exhash */

/* ------------------------------------------------------------------------- */
/* fix_directory_info - sync new inode numbers with directory info           */
/* Returns: 0 on success, -1 on failure                                      */
/* ------------------------------------------------------------------------- */
int fix_directory_info(struct gfs2_sbd *sbp, osi_list_t *dirs_to_fix)
{
	osi_list_t *tmp, *fix;
	struct inode_block *dir_iblk;
	uint64_t offset, dirblock;
	struct gfs2_inode *dip;
	struct gfs2_buffer_head *bh_dir;

	dirs_fixed = 0;
	dirents_fixed = 0;
	gettimeofday(&tv, NULL);
	seconds = tv.tv_sec;
	log_notice("\nFixing file and directory information.\n");
	fflush(stdout);
	offset = 0;
	tmp = NULL;
	/* for every directory in the list */
	for (fix = dirs_to_fix->next; fix != dirs_to_fix; fix = fix->next) {
		if (tmp) {
			osi_list_del(tmp);
			free(tmp);
		}
		tmp = fix; /* remember the addr to free next time */
		dirs_fixed++;
		/* figure out the directory inode block and read it in */
		dir_iblk = (struct inode_block *)fix;
		dirblock = dir_iblk->di_addr; /* addr of dir inode */
		/* read in the directory inode */
		bh_dir = bread(&sbp->buf_list, dirblock);
		dip = inode_get(sbp, bh_dir);
		/* fix the directory: either exhash (leaves) or linear (stuffed) */
		if (dip->i_di.di_flags & GFS2_DIF_EXHASH) {
			if (fix_one_directory_exhash(sbp, dip)) {
				log_crit("Error fixing exhash directory.\n");
				brelse(bh_dir, updated);
				return -1;
			}
		}
		else {
			if (process_dirent_info(dip, sbp, bh_dir, dip->i_di.di_entries)) {
				log_crit("Error fixing linear directory.\n");
				brelse(bh_dir, updated);
				return -1;
			}
		}
		brelse(bh_dir, updated);
	}
	/* Free the last entry in memory: */
	if (tmp) {
		osi_list_del(tmp);
		free(tmp);
	}
	return 0;
}/* fix_directory_info */

/* ------------------------------------------------------------------------- */
/* Fetch gfs1 jindex structure from buffer                                   */
/* ------------------------------------------------------------------------- */
void gfs1_jindex_in(struct gfs1_jindex *jindex, char *buf)
{
	struct gfs1_jindex *str = (struct gfs1_jindex *)buf;

	jindex->ji_addr = be64_to_cpu(str->ji_addr);
	jindex->ji_nsegment = be32_to_cpu(str->ji_nsegment);
	memset(jindex->ji_reserved, 0, 64);
}

/* ------------------------------------------------------------------------- */
/* read_gfs1_jiindex - read the gfs1 jindex file.                            */
/* Returns: 0 on success, -1 on failure                                      */
/* ------------------------------------------------------------------------- */
int read_gfs1_jiindex(struct gfs2_sbd *sdp)
{
	struct gfs2_inode *ip = sdp->md.jiinode;
	char buf[sizeof(struct gfs1_jindex)];
	unsigned int j;
	int error=0;

	if(ip->i_di.di_size % sizeof(struct gfs1_jindex) != 0){
		log_crit("The size reported in the journal index"
				" inode is not a\n"
				"\tmultiple of the size of a journal index.\n");
		return -1;
	}
	if(!(sd_jindex = (struct gfs1_jindex *)malloc(ip->i_di.di_size))) {
		log_crit("Unable to allocate journal index\n");
		return -1;
	}
	if(!memset(sd_jindex, 0, ip->i_di.di_size)) {
		log_crit("Unable to zero journal index\n");
		return -1;
	}
	for (j = 0; ; j++) {
		struct gfs1_jindex *journ;

		error = gfs2_readi(ip, buf, j * sizeof(struct gfs1_jindex),
						   sizeof(struct gfs1_jindex));
		if(!error)
			break;
		if (error != sizeof(struct gfs1_jindex)){
			log_crit("An error occurred while reading the"
					" journal index file.\n");
			goto fail;
		}
		journ = sd_jindex + j;
		gfs1_jindex_in(journ, buf);
		sdp->jsize = (journ->ji_nsegment * 16 * sdp->bsize) >> 20;
	}
	if(j * sizeof(struct gfs1_jindex) != ip->i_di.di_size){
		log_crit("journal inode size invalid\n");
		goto fail;
	}
	sdp->md.journals = sdp->orig_journals = j;
	return 0;

 fail:
	free(sd_jindex);
	return -1;
}

/* ------------------------------------------------------------------------- */
/* init - initialization code                                                */
/* Returns: 0 on success, -1 on failure                                      */
/* ------------------------------------------------------------------------- */
static int init(struct gfs2_sbd *sbp)
{
	struct gfs2_buffer_head *bh;
	int rgcount;
	struct gfs2_inum inum;

	memset(sbp, 0, sizeof(struct gfs2_sbd));
	if ((sbp->device_fd = open(device, O_RDWR)) < 0) {
		perror(device);
		exit(-1);
	}
	/* --------------------------------- */
	/* initialize the incore superblock  */
	/* --------------------------------- */
	sbp->sd_sb.sb_header.mh_magic = GFS2_MAGIC;
	sbp->sd_sb.sb_header.mh_type = GFS2_METATYPE_SB;
	sbp->sd_sb.sb_header.mh_format = GFS2_FORMAT_SB;

	osi_list_init((osi_list_t *)&dirs_to_fix);
	/* ---------------------------------------------- */
	/* Initialize lists and read in the superblock.   */
	/* ---------------------------------------------- */
	sbp->jsize = GFS2_DEFAULT_JSIZE;
	sbp->rgsize = GFS2_DEFAULT_RGSIZE;
	sbp->utsize = GFS2_DEFAULT_UTSIZE;
	sbp->qcsize = GFS2_DEFAULT_QCSIZE;
	sbp->time = time(NULL);
	sbp->blks_total = 0;   /* total blocks         - total them up later */
	sbp->blks_alloced = 0; /* blocks allocated     - total them up later */
	sbp->dinodes_alloced = 0; /* dinodes allocated - total them up later */
	sbp->sd_sb.sb_bsize = GFS2_DEFAULT_BSIZE;
	sbp->bsize = sbp->sd_sb.sb_bsize;
	osi_list_init(&sbp->rglist);
	init_buf_list(sbp, &sbp->buf_list, 128 << 20);
	init_buf_list(sbp, &sbp->nvbuf_list, 0xffffffff);
	compute_constants(sbp);

	bh = bread(&sbp->buf_list, GFS2_SB_ADDR >> sbp->sd_fsb2bb_shift);
	memcpy(&raw_gfs1_ondisk_sb, (struct gfs1_sb *)bh->b_data,
		   sizeof(struct gfs1_sb));
	gfs2_sb_in(&sbp->sd_sb, bh->b_data);
	sbp->bsize = sbp->sd_sb.sb_bsize;
	sbp->sd_inptrs = (sbp->bsize - sizeof(struct gfs_indirect)) /
		sizeof(uint64_t);
	sbp->sd_diptrs = (sbp->bsize - sizeof(struct gfs_dinode)) /
		sizeof(uint64_t);
	sbp->sd_jbsize = sbp->bsize - sizeof(struct gfs2_meta_header);
	brelse(bh, not_updated);
	sbp->sd_max_height = compute_heightsize(sbp, sbp->sd_heightsize,
						sbp->bsize, sbp->sd_diptrs,
						sbp->sd_inptrs);
	sbp->sd_max_jheight = compute_heightsize(sbp, sbp->sd_jheightsize,
						 sbp->sd_jbsize,
						 sbp->sd_diptrs,
						 sbp->sd_inptrs);
	/* -------------------------------------------------------- */
	/* Our constants are for gfs1.  Need some for gfs2 as well. */
	/* -------------------------------------------------------- */
	gfs2_inptrs = (sbp->bsize - sizeof(struct gfs2_meta_header)) /
                sizeof(uint64_t); /* How many ptrs can we fit on a block? */
	memset(gfs2_heightsize, 0, sizeof(gfs2_heightsize));
	gfs2_max_height = compute_heightsize(sbp, gfs2_heightsize,
					     sbp->bsize,
					     sbp->sd_diptrs, gfs2_inptrs);
	memset(gfs2_jheightsize, 0, sizeof(gfs2_jheightsize));
	gfs2_max_jheight = compute_heightsize(sbp, gfs2_jheightsize,
					      sbp->sd_jbsize,
					      sbp->sd_diptrs, gfs2_inptrs);

	/* ---------------------------------------------- */
	/* Make sure we're really gfs1                    */
	/* ---------------------------------------------- */
	if (sbp->sd_sb.sb_fs_format != GFS_FORMAT_FS ||
		sbp->sd_sb.sb_header.mh_type != GFS_METATYPE_SB ||
		sbp->sd_sb.sb_header.mh_format != GFS_FORMAT_SB ||
		sbp->sd_sb.sb_multihost_format != GFS_FORMAT_MULTI) {
		log_crit("Error: %s does not look like a gfs1 filesystem.\n",
				device);
		close(sbp->device_fd);
		exit(-1);
	}
	/* get gfs1 rindex inode - gfs1's rindex inode ptr became __pad2 */
	gfs2_inum_in(&inum, (char *)&raw_gfs1_ondisk_sb.sb_rindex_di);
	bh = bread(&sbp->buf_list, inum.no_addr);
	sbp->md.riinode = gfs_inode_get(sbp, bh);
	/* get gfs1 jindex inode - gfs1's journal index inode ptr became master */
	gfs2_inum_in(&inum, (char *)&raw_gfs1_ondisk_sb.sb_jindex_di);
	sbp->md.jiinode = gfs2_load_inode(sbp, inum.no_addr);
	/* read in the journal index data */
	read_gfs1_jiindex(sbp);
	/* read in the resource group index data: */

	/* We've got a slight dilemma here.  In gfs1, we used to have a meta */
	/* header in front of the rgindex pages.  In gfs2, we don't.  That's */
	/* apparently only for directories.  So we need to fake out libgfs2  */
	/* so that it adjusts for the metaheader by faking out the inode to  */
	/* look like a directory, temporarily.                               */
	sbp->md.riinode->i_di.di_mode &= ~S_IFMT;
	sbp->md.riinode->i_di.di_mode |= S_IFDIR;
	printf("Examining file system");
	if (gfs1_ri_update(sbp, 0, &rgcount)){
		log_crit("Unable to fill in resource group information.\n");
		return -1;
	}
	printf("\n");
	fflush(stdout);
	inode_put(sbp->md.riinode, updated);
	inode_put(sbp->md.jiinode, updated);
	log_debug("%d rgs found.\n", rgcount);
	return 0;
}/* fill_super_block */

/* ------------------------------------------------------------------------- */
/* give_warning - give the all-important warning message.                    */
/* ------------------------------------------------------------------------- */
void give_warning(void)
{
	printf("This program will convert a gfs1 filesystem to a "	\
		   "gfs2 filesystem.\n");
	printf("WARNING: This can't be undone.  It is strongly advised "	\
		   "that you:\n\n");
	printf("   1. Back up your entire filesystem first.\n");
	printf("   2. Run gfs_fsck first to ensure filesystem integrity.\n");
	printf("   3. Make sure the filesystem is NOT mounted from any node.\n");
	printf("   4. Make sure you have the latest software versions.\n");
}/* give_warning */

/* ------------------------------------------------------------------------- */
/* version  - print version information                                      */
/* ------------------------------------------------------------------------- */
void version(void)
{
	log_notice("gfs2_convert version %s (built %s %s)\n", RELEASE_VERSION,
			   __DATE__, __TIME__);
	log_notice("%s\n\n", REDHAT_COPYRIGHT);
}

/* ------------------------------------------------------------------------- */
/* usage - print usage information                                           */
/* ------------------------------------------------------------------------- */
void usage(const char *name)
{
	give_warning();
	printf("\nUsage:\n");
	printf("%s [-hnqvVy] <device>\n\n", name);
	printf("Flags:\n");
	printf("\th - print this help message\n");
	printf("\tn - assume 'no' to all questions\n");
	printf("\tq - quieter output\n");
	printf("\tv - more verbose output\n");
	printf("\tV - print version information\n");
	printf("\ty - assume 'yes' to all questions\n");
}/* usage */

/* ------------------------------------------------------------------------- */
/* process_parameters                                                        */
/* ------------------------------------------------------------------------- */
void process_parameters(int argc, char **argv, struct gfs2_options *opts)

{
	char c;

	opts->yes = 0;
	opts->no = 0;
	if (argc == 1) {
		usage(argv[0]);
		exit(0);
	}
	memset(device, 0, sizeof(device));
	while((c = getopt(argc, argv, "hnqvyV")) != -1) {
		switch(c) {

		case 'h':
			usage(argv[0]);
			exit(0);
			break;
		case 'n':
			opts->no = 1;
			break;
		case 'q':
			decrease_verbosity();
			break;
		case 'v':
			increase_verbosity();
			break;
		case 'V':
			exit(0);
		case 'y':
			opts->yes = 1;
			break;
		default:
			fprintf(stderr,"Parameter not understood: %c\n", c);
			usage(argv[0]);
			exit(0);
		}
	}
	if(argc > optind) {
		strcpy(device, argv[optind]);
		opts->device = device;
		if(!opts->device) {
			fprintf(stderr, "Please use '-h' for usage.\n");
			exit(1);
		}
	} else {
		fprintf(stderr, "No device specified.  Use '-h' for usage.\n");
		exit(1);
	}
} /* process_parameters */

/* ------------------------------------------------------------------------- */
/* rgrp_length - Calculate the length of a resource group                    */
/* @size: The total size of the resource group                               */
/* ------------------------------------------------------------------------- */
uint64_t rgrp_length(uint64_t size, struct gfs2_sbd *sdp)
{
	uint64_t bitbytes = RGRP_BITMAP_BLKS(&sdp->sd_sb) + 1;
	uint64_t stuff = RGRP_STUFFED_BLKS(&sdp->sd_sb) + 1;
	uint64_t blocks = 1;

	if (size >= stuff) {
		size -= stuff;
		while (size > bitbytes) {
			blocks++;
			size -= bitbytes;
		}
		if (size)
			blocks++;
	}
	return blocks;
}/* rgrp_length */

/* ------------------------------------------------------------------------- */
/* journ_space_to_rg - convert gfs1 journal space to gfs2 rg space.          */
/*                                                                           */
/* In gfs1, the journals were kept separate from the files and directories.  */
/* They had a dedicated section of the fs carved out for them.               */
/* In gfs2, the journals are just files like any other, (but still hidden).  */
/* Therefore, the old journal space has to be converted to normal resource   */
/* group space.                                                              */
/*                                                                           */
/* Returns: 0 on success, -1 on failure                                      */
/* ------------------------------------------------------------------------- */
int journ_space_to_rg(struct gfs2_sbd *sdp)
{
	int error = 0;
	int j, x;
	struct gfs1_jindex *jndx;
	struct rgrp_list *rgd, *rgdhigh;
	osi_list_t *tmp;
	struct gfs2_meta_header mh;

	mh.mh_magic = GFS2_MAGIC;
	mh.mh_type = GFS2_METATYPE_RB;
	mh.mh_format = GFS2_FORMAT_RB;
	log_notice("Converting journal space to rg space.\n");
	/* Go through each journal, converting them one by one */
	for (j = 0; j < sdp->orig_journals; j++) { /* for each journal */
		uint64_t size;

		jndx = &sd_jindex[j];
		/* go through all rg index entries, keeping track of the highest */
		/* that's still in the first subdevice.                          */
		/* Note: we really should go through all of the rgindex because  */
		/* we might have had rg's added by gfs_grow, and journals added  */
		/* by jadd.  gfs_grow adds rgs out of order, so we can't count   */
		/* on them being in ascending order.                             */
		rgdhigh = NULL;
		osi_list_foreach(tmp, &sdp->rglist) {
			rgd = osi_list_entry(tmp, struct rgrp_list, list);
			if (rgd->ri.ri_addr < jndx->ji_addr &&
				((rgdhigh == NULL) ||
				 (rgd->ri.ri_addr > rgdhigh->ri.ri_addr)))
				rgdhigh = rgd;
		} /* for each rg */
		log_info("Addr %" PRIx64 " comes after rg at addr %" PRIx64 "\n",
				 jndx->ji_addr, rgdhigh->ri.ri_addr);
		if (!rgdhigh) { /* if we somehow didn't find one. */
			log_crit("Error: No suitable rg found for journal.\n");
			return -1;
		}
		/* Allocate a new rgd entry which includes rg and ri. */
		/* convert the gfs1 rgrp into a new gfs2 rgrp */
		rgd = malloc(sizeof(struct rgrp_list));
		if (!rgd) {
			log_crit("Error: unable to allocate memory for rg conversion.\n");
			return -1;
		}
		memset(rgd, 0, sizeof(struct rgrp_list));
		size = jndx->ji_nsegment * be32_to_cpu(raw_gfs1_ondisk_sb.sb_seg_size);
		rgd->rg.rg_header.mh_magic = GFS2_MAGIC;
		rgd->rg.rg_header.mh_type = GFS2_METATYPE_RG;
		rgd->rg.rg_header.mh_format = GFS2_FORMAT_RG;
		rgd->rg.rg_flags = 0;
		rgd->rg.rg_dinodes = 0;

		rgd->ri.ri_addr = jndx->ji_addr; /* new rg addr becomes ji addr */
		rgd->ri.ri_length = rgrp_length(size, sdp); /* aka bitblocks */

		rgd->ri.ri_data0 = jndx->ji_addr + rgd->ri.ri_length;
		rgd->ri.ri_data = size - rgd->ri.ri_length;
		sdp->blks_total += rgd->ri.ri_data; /* For statfs file update */
		/* Round down to nearest multiple of GFS2_NBBY */
		while (rgd->ri.ri_data & 0x03)
			rgd->ri.ri_data--;
		rgd->rg.rg_free = rgd->ri.ri_data;
		rgd->ri.ri_bitbytes = rgd->ri.ri_data / GFS2_NBBY;
		convert_bitmaps(sdp, rgd, FALSE); /* allocates rgd->bh */
		for (x = 0; x < rgd->ri.ri_length; x++) {
			rgd->bh[x]->b_count++;
			if (x)
				gfs2_meta_header_out(&mh, rgd->bh[x]->b_data);
			else
				gfs2_rgrp_out(&rgd->rg, rgd->bh[x]->b_data);
		}
		/* Add the new gfs2 rg to our list: We'll output the rg index later. */
		osi_list_add_prev((osi_list_t *)&rgd->list,
						  (osi_list_t *)&sdp->rglist);
	} /* for each journal */
	return error;
}/* journ_space_to_rg */

/* ------------------------------------------------------------------------- */
/* update_inode_file - update the inode file with the new next_inum          */
/* ------------------------------------------------------------------------- */
void update_inode_file(struct gfs2_sbd *sdp)
{
	struct gfs2_inode *ip = sdp->md.inum;
	uint64_t buf;
	int count;
	
	buf = cpu_to_be64(sdp->md.next_inum);
	count = gfs2_writei(ip, &buf, 0, sizeof(uint64_t));
	if (count != sizeof(uint64_t))
		die("update_inode_file\n");
	
	log_debug("\nNext Inum: %"PRIu64"\n", sdp->md.next_inum);
}/* update_inode_file */

/* ------------------------------------------------------------------------- */
/* write_statfs_file - write the statfs file                                 */
/* ------------------------------------------------------------------------- */
void write_statfs_file(struct gfs2_sbd *sdp)
{
	struct gfs2_inode *ip = sdp->md.statfs;
	struct gfs2_statfs_change sc;
	char buf[sizeof(struct gfs2_statfs_change)];
	int count;
	
	sc.sc_total = sdp->blks_total;
	sc.sc_free = sdp->blks_total - sdp->blks_alloced;
	sc.sc_dinodes = sdp->dinodes_alloced;

	gfs2_statfs_change_out(&sc, buf);
	count = gfs2_writei(ip, buf, 0, sizeof(struct gfs2_statfs_change));
	if (count != sizeof(struct gfs2_statfs_change))
		die("do_init (2)\n");
}/* write_statfs_file */

/* ------------------------------------------------------------------------- */
/* remove_obsolete_gfs1 - remove obsolete gfs1 inodes.                       */
/* ------------------------------------------------------------------------- */
void remove_obsolete_gfs1(struct gfs2_sbd *sbp)
{
	struct gfs2_inum inum;

	log_notice("Removing obsolete GFS1 file system structures.\n");
	fflush(stdout);
	/* Delete the old gfs1 Journal index: */
	gfs2_inum_in(&inum, (char *)&raw_gfs1_ondisk_sb.sb_jindex_di);
	gfs2_freedi(sbp, inum.no_addr);

	/* Delete the old gfs1 rgindex: */
	gfs2_inum_in(&inum, (char *)&raw_gfs1_ondisk_sb.sb_rindex_di);
	gfs2_freedi(sbp, inum.no_addr);

	/* Delete the old gfs1 Quota file: */
	gfs2_inum_in(&inum, (char *)&raw_gfs1_ondisk_sb.sb_quota_di);
	gfs2_freedi(sbp, inum.no_addr);

	/* Delete the old gfs1 License file: */
	gfs2_inum_in(&inum, (char *)&raw_gfs1_ondisk_sb.sb_license_di);
	gfs2_freedi(sbp, inum.no_addr);
}

/* ------------------------------------------------------------------------- */
/* lifted from libgfs2/structures.c                                          */
/* ------------------------------------------------------------------------- */
void conv_build_jindex(struct gfs2_sbd *sdp)
{
	struct gfs2_inode *jindex;
	unsigned int j;

	jindex = createi(sdp->master_dir, "jindex", S_IFDIR | 0700,
			 GFS2_DIF_SYSTEM);

	for (j = 0; j < sdp->md.journals; j++) {
		char name[256];
		struct gfs2_inode *ip;

		printf("Writing journal #%d...", j + 1);
		fflush(stdout);
		sprintf(name, "journal%u", j);
		ip = createi(jindex, name, S_IFREG | 0600, GFS2_DIF_SYSTEM);
		write_journal(sdp, ip, j,
			      sdp->jsize << 20 >> sdp->sd_sb.sb_bsize_shift);
		inode_put(ip, updated);
		printf("done.\n");
		fflush(stdout);
	}

	if (sdp->debug) {
		printf("\nJindex:\n");
		gfs2_dinode_print(&jindex->i_di);
	}

	inode_put(jindex, updated);
}

/* ------------------------------------------------------------------------- */
/* main - mainline code                                                      */
/* ------------------------------------------------------------------------- */
int main(int argc, char **argv)
{
	int error;
	struct gfs2_buffer_head *bh;
	struct gfs2_options opts;

	version();
	process_parameters(argc, argv, &opts);
	error = init(&sb2);

	/* ---------------------------------------------- */
	/* Make them seal their fate.                     */
	/* ---------------------------------------------- */
	if (!error) {
		int abort;

		give_warning();
		if (!gfs2_query(&abort, &opts,
				"Convert %s from GFS1 to GFS2? (y/n)",
				device)) {
			log_crit("%s not converted.\n", device);
			close(sb2.device_fd);
			exit(0);
		}
	}
	/* ---------------------------------------------- */
	/* Convert incore gfs1 sb to gfs2 sb              */
	/* ---------------------------------------------- */
	if (!error) {
		log_notice("Converting resource groups.");
		fflush(stdout);
		error = convert_rgs(&sb2);
		log_notice("\n");
		if (error)
			log_crit("%s: Unable to convert resource groups.\n",
					device);
		bcommit(&sb2.nvbuf_list); /* write the buffers to disk */
	}
	/* ---------------------------------------------- */
	/* Renumber the inodes consecutively.             */
	/* ---------------------------------------------- */
	if (!error) {
		error = inode_renumber(&sb2, sb2.sd_sb.sb_root_dir.no_addr);
		if (error)
			log_crit("\n%s: Error renumbering inodes.\n", device);
		bcommit(&sb2.buf_list); /* write the buffers to disk */
	}
	/* ---------------------------------------------- */
	/* Fix the directories to match the new numbers.  */
	/* ---------------------------------------------- */
	if (!error) {
		error = fix_directory_info(&sb2, (osi_list_t *)&dirs_to_fix);
		log_notice("\r%" PRIu64 " directories, %" PRIu64 " dirents fixed.",
				   dirs_fixed, dirents_fixed);
		fflush(stdout);
		if (error)
			log_crit("\n%s: Error fixing directories.\n", device);
	}
	/* ---------------------------------------------- */
	/* Convert journal space to rg space              */
	/* ---------------------------------------------- */
	if (!error) {
		log_notice("\nConverting journals.\n");
		error = journ_space_to_rg(&sb2);
		if (error)
			log_crit("%s: Error converting journal space.\n", device);
		bcommit(&sb2.buf_list); /* write the buffers to disk */
	}
	/* ---------------------------------------------- */
	/* Create our system files and directories.       */
	/* ---------------------------------------------- */
	if (!error) {
		/* Now we've got to treat it as a gfs2 file system */
		compute_constants(&sb2);
		/* Build the master subdirectory. */
		build_master(&sb2); /* Does not do inode_put */
		sb2.sd_sb.sb_master_dir = sb2.master_dir->i_di.di_num;
		/* Build empty journal index file. */
		conv_build_jindex(&sb2);
		log_notice("Building GFS2 file system structures.\n");
		/* Build the per-node directories */
		build_per_node(&sb2);
		/* Create the empty inode number file */
		build_inum(&sb2); /* Does not do inode_put */
		/* Create the statfs file */
		build_statfs(&sb2); /* Does not do inode_put */

		/* Create the resource group index file */
		build_rindex(&sb2);
		/* Create the quota file */
		build_quota(&sb2);

		update_inode_file(&sb2);
		write_statfs_file(&sb2);

		inode_put(sb2.master_dir, updated);
		inode_put(sb2.md.inum, updated);
		inode_put(sb2.md.statfs, updated);

		bcommit(&sb2.buf_list); /* write the buffers to disk */
		bcommit(&sb2.nvbuf_list); /* write the buffers to disk */

		/* Now delete the now-obsolete gfs1 files: */
		remove_obsolete_gfs1(&sb2);
		/* Now free all the in memory */
		gfs2_rgrp_free(&sb2.rglist, updated);
		log_notice("Committing changes to disk.\n");
		fflush(stdout);
		/* Set filesystem type in superblock to gfs2.  We do this at the */
		/* end because if the tool is interrupted in the middle, we want */
		/* it to not reject the partially converted fs as already done   */
		/* when it's run a second time.                                  */
		bh = bread(&sb2.buf_list, sb2.sb_addr);
		sb2.sd_sb.sb_fs_format = GFS2_FORMAT_FS;
		sb2.sd_sb.sb_multihost_format = GFS2_FORMAT_MULTI;
		gfs2_sb_out(&sb2.sd_sb, bh->b_data);
		brelse(bh, updated);

		bsync(&sb2.buf_list); /* write the buffers to disk */
		bsync(&sb2.nvbuf_list); /* write the buffers to disk */
		error = fsync(sb2.device_fd);
		if (error)
			perror(device);
		else
			log_notice("%s: filesystem converted successfully to gfs2.\n",
					   device);
	}
	close(sb2.device_fd);
	if (sd_jindex)
		free(sd_jindex);
	exit(0);
}
