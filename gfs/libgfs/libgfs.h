/* These make the library more palatable to userland */

#ifndef LIBGFS_H
#define LIBGFS_H

#include <stdint.h>
#include "osi_user.h"
#include "incore.h"

struct qstr {
        unsigned int hash;
        unsigned int len;
        const unsigned char *name;
};
#define up(x) ;
#define up_write(x) ;
#define down(x) ;
#define down_write(x) ;
#define printk printf

/* ------------------------------------------------------------------------- */
/* formerly bitmap.h:                                                        */
/* ------------------------------------------------------------------------- */
struct bmap {
	uint64_t size;
	uint64_t mapsize;
	int chunksize;
	int chunks_per_byte;
	char *map;
};

int bitmap_create(struct bmap *bmap, uint64_t size, uint8_t bitsize);
int bitmap_set(struct bmap *bmap, uint64_t offset, uint8_t val);
int bitmap_get(struct bmap *bmap, uint64_t bit, uint8_t *val);
int bitmap_clear(struct bmap *bmap, uint64_t offset);
void bitmap_destroy(struct bmap *bmap);
uint64_t bitmap_size(struct bmap *bmap);

/* ------------------------------------------------------------------------- */
/* formerly block_list.h:                                                    */
/* ------------------------------------------------------------------------- */
#define BMAP_COUNT 13

enum block_list_type {
	gbmap = 0,  /* Grouped bitmap */
	dbmap,	    /* Ondisk bitmap - like grouped bitmap, but mmaps
		     * the bitmaps onto file(s) ondisk - not implemented */
};

/* Must be kept in sync with mark_to_bitmap array in block_list.c */
enum mark_block {
	block_free = 0,
	block_used,
	indir_blk,
	inode_dir,
	inode_file,
	inode_lnk,
	inode_blk,
	inode_chr,
	inode_fifo,
	inode_sock,
	leaf_blk,
	journal_blk,
	meta_other,
	meta_free,
	meta_eattr,
	meta_inval = 15,
	bad_block,	/* Contains at least one bad block */
	dup_block,	/* Contains at least one duplicate block */
	eattr_block,	/* Contains an eattr */
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

/* ------------------------------------------------------------------------- */
/* formerly log.h:                                                           */
/* ------------------------------------------------------------------------- */
#define MSG_DEBUG	7
#define MSG_INFO	6
#define MSG_NOTICE	5
#define MSG_WARN	4
#define MSG_ERROR	3
#define MSG_CRITICAL	2
#define MSG_NULL	1

struct options;

#define print_log(iif, priority, format...)	\
do { \
	print_log_level(iif, priority, __FILE__, __LINE__, ## format);	\
} while(0)

#define log_debug(format...) \
do { \
	print_log(0, MSG_DEBUG, format);		\
} while(0)

#define log_info(format...) \
do { \
	print_log(0, MSG_INFO, format);		\
} while(0)

#define log_notice(format...) \
do { \
	print_log(0, MSG_NOTICE, format);	\
} while(0)

#define log_warn(format...) \
do { \
	print_log(0, MSG_WARN, format);		\
} while(0)

#define log_err(format...) \
do { \
	print_log(0, MSG_ERROR, format);		\
} while(0)

#define log_crit(format...) \
do { \
	print_log(0, MSG_CRITICAL, format);	\
} while(0)

#define stack log_debug("<backtrace> - %s()\n", __func__)

#define log_at_debug(format...)		\
do { \
	print_log(1, MSG_DEBUG, format);	\
} while(0)

#define log_at_info(format...) \
do { \
	print_log(1, MSG_INFO, format);		\
} while(0)

#define log_at_notice(format...) \
do { \
	print_log(1, MSG_NOTICE, format);	\
} while(0)

#define log_at_warn(format...) \
do { \
	print_log(1, MSG_WARN, format);		\
} while(0)

#define log_at_err(format...) \
do { \
	print_log(1, MSG_ERROR, format);		\
} while(0)

#define log_at_crit(format...) \
do { \
	print_log(1, MSG_CRITICAL, format);	\
} while(0)

void increase_verbosity(void);
void decrease_verbosity(void);
void print_log_level(int iif, int priority, char *file, int line, const char *format, ...);
int query(struct options *opts, const char *format, ...);

/* ------------------------------------------------------------------------- */
/* formerly bio.h:                                                           */
/* ------------------------------------------------------------------------- */
/* buf_write flags */
#define BW_WAIT 1


#define BH_DATA(bh) ((char *)(bh)->b_data)
#define BH_BLKNO(bh) ((uint64)(bh)->b_blocknr)
#define BH_SIZE(bh) ((uint32)(bh)->b_size)
#define BH_STATE(bh) ((uint32)(bh)->b_state)

int get_buf(uint32_t sb_bsize, uint64 blkno, osi_buf_t **bhp);
void relse_buf(osi_buf_t *bh);
int read_buf(int disk_fd, osi_buf_t *bh, int flags);
int write_buf(int disk_fd, osi_buf_t *bh, int flags);
int get_and_read_buf(int disk_fd, uint32_t sb_bsize, uint64 blkno,
					 osi_buf_t **bhp, int flags);

/* ------------------------------------------------------------------------- */
/* formerly fs_bits.h:                                                       */
/* ------------------------------------------------------------------------- */
#define BFITNOENT (0xFFFFFFFF)

struct fs_bitmap
{
	uint32   bi_offset;	/* The offset in the buffer of the first byte */
	uint32   bi_start;      /* The position of the first byte in this block */
	uint32   bi_len;        /* The number of bytes in this block */
};
typedef struct fs_bitmap fs_bitmap_t;

/* functions with blk #'s that are buffer relative */
uint32_t fs_bitcount(unsigned char *buffer, unsigned int buflen,
		     unsigned char state);
uint32_t fs_bitfit(unsigned char *buffer, unsigned int buflen,
		   uint32_t goal, unsigned char old_state);

/* functions with blk #'s that are rgrp relative */
uint32_t fs_blkalloc_internal(struct gfs_rgrpd *rgd, uint32_t goal,
			      unsigned char old_state,
			      unsigned char new_state, int do_it);

/* functions with blk #'s that are file system relative */
int fs_get_bitmap(int disk_fd, struct gfs_sbd *sdp, uint64_t blkno, struct gfs_rgrpd *rgd);
int fs_set_bitmap(int disk_fd, struct gfs_sbd *sdp, uint64_t blkno, int state);

/* ------------------------------------------------------------------------- */
/* formerly fs_bmap.h                                                        */
/* ------------------------------------------------------------------------- */
int fs_unstuff_dinode(int disk_fd, struct gfs_inode *ip);
int fs_block_map(int disk_fd, struct gfs_inode *ip, uint64 lblock, int *new,
		 uint64 *dblock, uint32 *extlen);

/* ------------------------------------------------------------------------- */
/* formerly iddev.h                                                          */
/* ------------------------------------------------------------------------- */
int device_size(int fd, uint64_t *bytes);

/* ------------------------------------------------------------------------- */
/* formerly util.h:                                                          */
/* ------------------------------------------------------------------------- */
#define do_lseek(fd, off) \
  ((lseek((fd), (off), SEEK_SET) == (off)) ? 0 : -1)

#define do_read(fd, buff, len) \
  ((read((fd), (buff), (len)) == (len)) ? 0 : -1)

#define do_write(fd, buff, len) \
  ((write((fd), (buff), (len)) == (len)) ? 0 : -1)

int compute_height(struct gfs_sbd *sdp, uint64 sz);
int check_range(struct gfs_sbd *sdp, uint64 blkno);
int set_meta(osi_buf_t *bh, int type, int format);
int check_type(osi_buf_t *bh, int type);
int check_meta(osi_buf_t *bh, int type);
int next_rg_meta(struct gfs_rgrpd *rgd, uint64 *block, int first);
int next_rg_meta_free(struct gfs_rgrpd *rgd, uint64 *block, int first, int *free);
int next_rg_metatype(int disk_fd, struct gfs_rgrpd *rgd, uint64 *block, uint32 type, int first);
struct di_info *search_list(osi_list_t *list, uint64 addr);

/* ------------------------------------------------------------------------- */
/* formerly rgrp.h:                                                          */
/* ------------------------------------------------------------------------- */
struct gfs_sbd;
struct gfs_rgrpd;
struct gfs_inode;

int fs_compute_bitstructs(struct gfs_rgrpd *rgd);
struct gfs_rgrpd *fs_blk2rgrpd(struct gfs_sbd *sdp, uint64_t blk);

int fs_rgrp_read(int disk_fd, struct gfs_rgrpd *rgd, int repair_if_corrupted);
void fs_rgrp_relse(struct gfs_rgrpd *rgd);
int fs_rgrp_verify(struct gfs_rgrpd *rgd);
int fs_rgrp_recount(int disk_fd, struct gfs_rgrpd *rgd);

int clump_alloc(int disk_fd, struct gfs_rgrpd *rgd, uint32_t goal);
int fs_blkalloc(int disk_fd, struct gfs_inode *ip, uint64_t *block);
int fs_metaalloc(int disk_fd, struct gfs_inode *ip, uint64_t *block);

/* ------------------------------------------------------------------------- */
/* formerly file.h:                                                          */
/* ------------------------------------------------------------------------- */
int readi(int disk_fd, struct gfs_inode *ip, void *buf, uint64_t offset,
		  unsigned int size);
int writei(int disk_fd, struct gfs_inode *ip, void *buf, uint64_t offset,
		   unsigned int size);

/* ------------------------------------------------------------------------- */
/* formerly inode.h:                                                         */
/* ------------------------------------------------------------------------- */
int copyin_inode(struct gfs_sbd *sbp, osi_buf_t *bh, struct gfs_inode **ip);
int load_inode(int disk_fd, struct gfs_sbd *sbp, uint64_t block,
			   struct gfs_inode **ip);
void free_inode(struct gfs_inode **inode);
int check_inode(struct gfs_inode *ip);
int create_inode(int disk_fd, struct gfs_sbd *sbp, unsigned int type,
				 struct gfs_inode **ip);
int make_dinode(int disk_fd, struct gfs_inode *dip,
					   struct gfs_sbd *sdp, struct gfs_inum *inum,
				unsigned int type, unsigned int mode, osi_cred_t *cred);

/* ------------------------------------------------------------------------- */
/* formerly fs_inode.h:                                                      */
/* ------------------------------------------------------------------------- */
int fs_copyin_dinode(int disk_fd, uint32_t sb_bsize, struct gfs_inode *ip, osi_buf_t *bh);
int fs_copyout_dinode(int disk_fd, uint32_t sb_bsize, struct gfs_inode *ip);
int fs_mkdir(int disk_fd, struct gfs_inode *dip, char *new_dir, int mode, struct gfs_inode **nip);

static __inline__ int fs_is_stuffed(struct gfs_inode *ip)
{
	return !ip->i_di.di_height;
}

static __inline__ int fs_is_jdata(struct gfs_inode *ip)
{
	return ip->i_di.di_flags & GFS_DIF_JDATA;
}

/* ------------------------------------------------------------------------- */
/* formerly fs_dir.h:                                                        */
/* ------------------------------------------------------------------------- */
#define ID_FILENAME 0
#define ID_INUM     1
typedef struct identifier_s {
	int type;

	osi_filename_t *filename;
	struct gfs_inum *inum;
} identifier_t;

int dirent_del(int disk_fd, struct gfs_inode *dip, osi_buf_t *bh,
	       struct gfs_dirent *prev, struct gfs_dirent *cur);
int gfs_inode_is_stuffed(struct gfs_inode *ip);
int dirent_first(osi_buf_t *bh, struct gfs_dirent **dent);
int dirent_next(osi_buf_t *bh, struct gfs_dirent **dent);
int get_leaf_nr(int file_fd, struct gfs_inode *dip, uint32 index,
				uint64 *leaf_out);
int fs_filecmp(osi_filename_t *file1, char *file2, int len_of_file2);
int fs_dirent_del(int disk_fd, struct gfs_inode *dip, osi_buf_t *bh, osi_filename_t *filename);
int fs_dir_add(int disk_fd, struct gfs_inode *dip, osi_filename_t *filename,
	       struct gfs_inum *inum, unsigned int type);
int fs_dirent_alloc(int disk_fd, struct gfs_inode *dip, osi_buf_t *bh,
		    int name_len, struct gfs_dirent **dent_out);

int fs_dir_search(int disk_fd, struct gfs_inode *dip, identifier_t *id, unsigned int *type);
int get_first_leaf(int disk_fd, struct gfs_inode *dip, uint32 index,
				   osi_buf_t **bh_out);
int get_next_leaf(int disk_fd, struct gfs_inode *dip,osi_buf_t *bh_in,osi_buf_t **bh_out);
int get_leaf(int disk_fd, struct gfs_inode *dip, uint64 leaf_no,
			 osi_buf_t **bhp);

/* ------------------------------------------------------------------------- */
/* formerly super.h:                                                         */
/* ------------------------------------------------------------------------- */
int read_sb(int disk_fd, struct gfs_sbd *sdp);
int ji_update(int disk_fd, struct gfs_sbd *sdp);
int ri_update(int disk_fd, struct gfs_sbd *sdp);
int write_sb(int disk_fd, struct gfs_sbd *sdp);
int set_block_ranges(int disk_fd, struct gfs_sbd *sdp);
int read_super_block(int disk_fd, struct gfs_sbd *sdp);

/* ------------------------------------------------------------------------- */
/* formerly rgrp.h:                                                          */
/* ------------------------------------------------------------------------- */

#endif
