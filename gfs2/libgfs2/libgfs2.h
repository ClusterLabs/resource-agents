#ifndef __LIBGFS2_DOT_H__
#define __LIBGFS2_DOT_H__

#include <inttypes.h>
#include <sys/types.h>
#include <linux/types.h>
#include <linux/limits.h>

#include "linux_endian.h"
#include <linux/gfs2_ondisk.h>
#include "osi_list.h"
#include "copyright.cf"
#include "ondisk.h"

#ifndef TRUE
#define TRUE (1)
#endif

#ifndef FALSE
#define FALSE (0)
#endif

#define die(fmt, args...) \
do { \
	fprintf(stderr, "%s: ", prog_name); \
	fprintf(stderr, fmt, ##args); \
	exit(EXIT_FAILURE); \
} while (0)

#define do_lseek(fd, off) \
{ \
  if (lseek((fd), (off), SEEK_SET) != (off)) \
    die("bad seek: %s on line %d of file %s\n", \
	strerror(errno),__LINE__, __FILE__); \
}

#define do_read(fd, buff, len) \
{ \
  if (read((fd), (buff), (len)) < 0) \
    die("bad read: %s on line %d of file %s\n", \
	strerror(errno), __LINE__, __FILE__); \
}

#define do_write(fd, buff, len) \
{ \
  if (write((fd), (buff), (len)) != (len)) \
    die("bad write: %s on line %d of file %s\n", \
	strerror(errno), __LINE__, __FILE__); \
}

#define zalloc(ptr, size) \
do { \
	(ptr) = malloc((size)); \
	if ((ptr)) \
		memset((ptr), 0, (size)); \
	else \
		die("unable to allocate memory on line %d of file %s\n", \
		    __LINE__, __FILE__); \
} while (0)

#define DIV_RU(x, y) (((x) + (y) - 1) / (y))

static __inline__ uint64_t do_div_i(uint64_t *num, unsigned int den)
{
	unsigned int m = *num % den;
	*num /= den;
	return m;
}
#define do_div(n, d) do_div_i(&(n), (d))

#define SRANDOM do { srandom(time(NULL) ^ getpid()); } while (0)
#define RANDOM(values) ((values) * (random() / (RAND_MAX + 1.0)))

struct device {
	uint64_t start;
	uint64_t length;
	uint32_t rgf_flags;
};

struct gfs2_bitmap
{
	uint32_t   bi_offset;  /* The offset in the buffer of the first byte */
	uint32_t   bi_start;   /* The position of the first byte in this block */
	uint32_t   bi_len;     /* The number of bytes in this block */
};
typedef struct gfs2_bitmap gfs2_bitmap_t;

struct rgrp_list {
	osi_list_t list;
	uint64_t start;	   /* The offset of the beginning of this resource group */
	uint64_t length;	/* The length of this resource group */
	uint32_t rgf_flags;

	struct gfs2_rindex ri;
	struct gfs2_rgrp rg;
	gfs2_bitmap_t *bits;
	struct gfs2_buffer_head **bh;
};

struct gfs2_buffer_head {
	osi_list_t b_list;
	osi_list_t b_hash;
	osi_list_t b_altlist; /* alternate list */

	unsigned int b_count;
	uint64_t b_blocknr;
	char *b_data;

	int b_changed;
};

struct special_blocks {
	osi_list_t list;
	uint64_t block;
};

struct gfs2_sbd;
struct gfs2_inode {
	struct gfs2_dinode i_di;
	struct gfs2_buffer_head *i_bh;
	struct gfs2_sbd *i_sbd;
};

#define BUF_HASH_SHIFT       (13)    /* # hash buckets = 8K */
#define BUF_HASH_SIZE        (1 << BUF_HASH_SHIFT)
#define BUF_HASH_MASK        (BUF_HASH_SIZE - 1)

/* FIXME not sure that i want to keep a record of the inodes or the
 * contents of them, or both ... if I need to write back to them, it
 * would be easier to hold the inode as well  */
struct per_node
{
	struct gfs2_inode *inum;
	struct gfs2_inum_range inum_range;
	struct gfs2_inode *statfs;
	struct gfs2_statfs_change statfs_change;
	struct gfs2_inode *unlinked;
	struct gfs2_inode *quota;
	struct gfs2_quota_change quota_change;
};

struct master_dir
{
	struct gfs2_inode *inum;
	uint64_t next_inum;
	struct gfs2_inode *statfs;
	struct gfs2_statfs_change statfs_change;

	struct gfs2_rindex rindex;
	struct gfs2_inode *qinode;
	struct gfs2_quota quotas;

	struct gfs2_inode       *jiinode;
	struct gfs2_inode       *riinode;
	struct gfs2_inode       *rooti;
	struct gfs2_inode       *pinode;
	
	struct gfs2_inode **journal;      /* Array of journals */
	uint32_t journals;                /* Journal count */
	struct per_node *pn;              /* Array of per_node entries */
};

struct gfs2_sbd {
	struct gfs2_sb sd_sb;    /* a copy of the ondisk structure */
	char lockproto[GFS2_LOCKNAME_LEN];
	char locktable[GFS2_LOCKNAME_LEN];

	unsigned int bsize;	     /* The block size of the FS (in bytes) */
	unsigned int jsize;	     /* Size of journals (in MB) */
	unsigned int rgsize;     /* Size of resource groups (in MB) */
	unsigned int utsize;     /* Size of unlinked tag files (in MB) */
	unsigned int qcsize;     /* Size of quota change files (in MB) */

	int debug;
	int quiet;
	int expert;
	int override;

	char device_name[PATH_MAX];
	char *path_name;

	/* Constants */

	uint32_t sd_fsb2bb;
	uint32_t sd_fsb2bb_shift;
	uint32_t sd_diptrs;
	uint32_t sd_inptrs;
	uint32_t sd_jbsize;
	uint32_t sd_hash_bsize;
	uint32_t sd_hash_bsize_shift;
	uint32_t sd_hash_ptrs;
	uint32_t sd_max_dirres;
	uint32_t sd_max_height;
	uint64_t sd_heightsize[GFS2_MAX_META_HEIGHT];
	uint32_t sd_max_jheight;
	uint64_t sd_jheightsize[GFS2_MAX_META_HEIGHT];

	/* Not specified on the command line, but... */

	int64_t time;

	struct device device;
	uint64_t device_size;

	int device_fd;
	int path_fd;

	uint64_t sb_addr;

	uint64_t orig_fssize;
	uint64_t fssize;
	uint64_t blks_total;
	uint64_t blks_alloced;
	uint64_t dinodes_alloced;

	uint64_t orig_rgrps;
	uint64_t rgrps;
	uint64_t new_rgrps;
	osi_list_t rglist;

	unsigned int orig_journals;

	unsigned int num_bufs;
	osi_list_t buf_list;
	osi_list_t buf_hash[BUF_HASH_SIZE];

	struct gfs2_inode *master_dir;
	struct master_dir md;

	unsigned int spills;
	unsigned int writes;
	int metafs_fd;
	char metafs_path[PATH_MAX]; /* where metafs is mounted */
	struct special_blocks bad_blocks;
	struct special_blocks dup_blocks;
	struct special_blocks eattr_blocks;
};

extern char *prog_name;

#define GFS2_DEFAULT_BSIZE          (4096)
#define GFS2_DEFAULT_JSIZE          (128)
#define GFS2_DEFAULT_RGSIZE         (256)
#define GFS2_DEFAULT_UTSIZE         (1)
#define GFS2_DEFAULT_QCSIZE         (1)
#define GFS2_DEFAULT_LOCKPROTO      "lock_dlm"
#define GFS2_MIN_GROW_SIZE          (10)
#define GFS2_EXCESSIVE_RGS          (10000)

#define DATA (1)
#define META (2)
#define DINODE (3)

#define NOT_UPDATED (0)
#define UPDATED (1)

/* A bit of explanation is in order: */
/* updated flag means the buffer was updated from THIS function before */
/*         brelse was called. */
/* not_updated flag means the buffer may or may not have been updated  */
/*         by a function called within this one, but it wasn't updated */
/*         by this function. */
enum update_flags {
	not_updated = NOT_UPDATED,
	updated = UPDATED
};

/* bitmap.c */
struct gfs2_bmap {
        uint64_t size;
        uint64_t mapsize;
        int chunksize;
        int chunks_per_byte;
        char *map;
};

int gfs2_bitmap_create(struct gfs2_bmap *bmap, uint64_t size, uint8_t bitsize);
int gfs2_bitmap_set(struct gfs2_bmap *bmap, uint64_t offset, uint8_t val);
int gfs2_bitmap_get(struct gfs2_bmap *bmap, uint64_t bit, uint8_t *val);
int gfs2_bitmap_clear(struct gfs2_bmap *bmap, uint64_t offset);
void gfs2_bitmap_destroy(struct gfs2_bmap *bmap);
uint64_t gfs2_bitmap_size(struct gfs2_bmap *bmap);

/* block_list.c */
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
#define UNUSED1         (0xE)  /*   1110 */
#define INVALID_META    (0xF)  /*   1111 */

/* Must be kept in sync with mark_to_bitmap array in block_list.c */
enum gfs2_mark_block {
	gfs2_block_free = FREE,
	gfs2_block_used = BLOCK_IN_USE,
	gfs2_indir_blk = DIR_INDIR_BLK,
	gfs2_inode_dir = DIR_INODE,
	gfs2_inode_file = FILE_INODE,
	gfs2_inode_lnk = LNK_INODE,
	gfs2_inode_blk = BLK_INODE,
	gfs2_inode_chr = CHR_INODE,
	gfs2_inode_fifo = FIFO_INODE,
	gfs2_inode_sock = SOCK_INODE,
	gfs2_leaf_blk = DIR_LEAF_INODE,
	gfs2_journal_blk = JOURNAL_BLK,
	gfs2_meta_other = OTHER_META,
	gfs2_meta_eattr = EATTR_META,
	gfs2_meta_unused = UNUSED1,
	gfs2_meta_inval = INVALID_META,
	gfs2_bad_block,      /* Contains at least one bad block */
	gfs2_dup_block,      /* Contains at least one duplicate block */
	gfs2_eattr_block,    /* Contains an eattr */
};

struct gfs2_block_query {
        uint8_t block_type;
        uint8_t bad_block;
        uint8_t dup_block;
        uint8_t eattr_block;
};

struct gfs2_gbmap {
        struct gfs2_bmap group_map;
        struct gfs2_bmap bad_map;
        struct gfs2_bmap dup_map;
        struct gfs2_bmap eattr_map;
};

union gfs2_block_lists {
        struct gfs2_gbmap gbmap;
};

/* bitmap implementation */
struct gfs2_block_list {
        union gfs2_block_lists list;
};

struct gfs2_block_list *gfs2_block_list_create(struct gfs2_sbd *sdp,
					       uint64_t size,
					       uint64_t *addl_mem_needed);
struct special_blocks *blockfind(struct special_blocks *blist, uint64_t num);
void gfs2_special_set(struct special_blocks *blocklist, uint64_t block);
void gfs2_special_clear(struct special_blocks *blocklist, uint64_t block);
void gfs2_special_free(struct special_blocks *blist);
int gfs2_block_mark(struct gfs2_sbd *sdp, struct gfs2_block_list *il,
		    uint64_t block, enum gfs2_mark_block mark);
int gfs2_block_set(struct gfs2_sbd *sdp, struct gfs2_block_list *il,
		   uint64_t block, enum gfs2_mark_block mark);
int gfs2_block_clear(struct gfs2_sbd *sdp, struct gfs2_block_list *il,
		     uint64_t block, enum gfs2_mark_block m);
int gfs2_block_check(struct gfs2_sbd *sdp, struct gfs2_block_list *il,
		     uint64_t block, struct gfs2_block_query *val);
void *gfs2_block_list_destroy(struct gfs2_sbd *sdp,
			      struct gfs2_block_list *il);

/* buf.c */
struct gfs2_buffer_head *bget_generic(struct gfs2_sbd *sdp, uint64_t num,
				      int find_existing, int read_disk);
struct gfs2_buffer_head *bget(struct gfs2_sbd *sdp, uint64_t num);
struct gfs2_buffer_head *bread(struct gfs2_sbd *sdp, uint64_t num);
struct gfs2_buffer_head *bget_zero(struct gfs2_sbd *sdp, uint64_t num);
struct gfs2_buffer_head *bhold(struct gfs2_buffer_head *bh);
void brelse(struct gfs2_buffer_head *bh, enum update_flags updated);
void bsync(struct gfs2_sbd *sdp);
void bcommit(struct gfs2_sbd *sdp);
void bcheck(struct gfs2_sbd *sdp);
void write_buffer(struct gfs2_sbd *sdp, struct gfs2_buffer_head *bh);

/* device_geometry.c */
void device_geometry(struct gfs2_sbd *sdp);
void fix_device_geometry(struct gfs2_sbd *sdp);

/* fs_bits.c */
#define BFITNOENT (0xFFFFFFFF)

/* functions with blk #'s that are buffer relative */
uint32_t gfs2_bitcount(unsigned char *buffer, unsigned int buflen,
                     unsigned char state);
uint32_t gfs2_bitfit(unsigned char *buffer, unsigned int buflen,
		     uint32_t goal, unsigned char old_state);

/* functions with blk #'s that are rgrp relative */
uint32_t gfs2_blkalloc_internal(struct rgrp_list *rgd, uint32_t goal,
				unsigned char old_state,
				unsigned char new_state, int do_it);
int gfs2_check_range(struct gfs2_sbd *sdp, uint64_t blkno);

/* functions with blk #'s that are file system relative */
int gfs2_get_bitmap(struct gfs2_sbd *sdp, uint64_t blkno,
                                        struct rgrp_list *rgd);
int gfs2_set_bitmap(struct gfs2_sbd *sdp, uint64_t blkno, int state);

/* fs_geometry.c */
void rgblocks2bitblocks(unsigned int bsize, uint32_t *rgblocks,
			uint32_t *bitblocks);
uint64_t how_many_rgrps(struct gfs2_sbd *sdp, struct device *dev,
			int rgsize_specified);
void compute_rgrp_layout(struct gfs2_sbd *sdp, int rgsize_specified);
void build_rgrps(struct gfs2_sbd *sdp, int write);

/* fs_ops.c */
#define IS_LEAF     (1)
#define IS_DINODE   (2)

struct gfs2_inode *inode_get(struct gfs2_sbd *sdp,
			     struct gfs2_buffer_head *bh);
void inode_put(struct gfs2_inode *ip, enum update_flags updated);
uint64_t data_alloc(struct gfs2_inode *ip);
uint64_t meta_alloc(struct gfs2_inode *ip);
uint64_t dinode_alloc(struct gfs2_sbd *sdp);
int gfs2_readi(struct gfs2_inode *ip, void *buf,
			   uint64_t offset, unsigned int size);
int gfs2_writei(struct gfs2_inode *ip, void *buf,
				uint64_t offset, unsigned int size);
struct gfs2_buffer_head *get_file_buf(struct gfs2_inode *ip, uint64_t lbn,
				      int prealloc);
struct gfs2_buffer_head *init_dinode(struct gfs2_sbd *sdp,
				     struct gfs2_inum *inum,
				     unsigned int mode, uint32_t flags,
				     struct gfs2_inum *parent);
struct gfs2_inode *createi(struct gfs2_inode *dip, char *filename,
			   unsigned int mode, uint32_t flags);
void dirent2_del(struct gfs2_inode *dip, struct gfs2_buffer_head *bh,
		 struct gfs2_dirent *prev, struct gfs2_dirent *cur);
struct gfs2_inode *gfs2_load_inode(struct gfs2_sbd *sbp, uint64_t block);
int gfs2_lookupi(struct gfs2_inode *dip, const char *filename, int len,
		 struct gfs2_inode **ipp);
void dir_add(struct gfs2_inode *dip, char *filename, int len,
			 struct gfs2_inum *inum, unsigned int type);
int gfs2_dirent_del(struct gfs2_inode *dip, struct gfs2_buffer_head *bh,
		    const char *filename, int filename_len);
void block_map(struct gfs2_inode *ip, uint64_t lblock, int *new,
	       uint64_t *dblock, uint32_t *extlen, int prealloc,
	       enum update_flags if_changed);
void gfs2_get_leaf_nr(struct gfs2_inode *dip, uint32_t index,
					  uint64_t *leaf_out);
void gfs2_put_leaf_nr(struct gfs2_inode *dip, uint32_t inx, uint64_t leaf_out);

int gfs2_freedi(struct gfs2_sbd *sdp, uint64_t block);
int gfs2_get_leaf(struct gfs2_inode *dip, uint64_t leaf_no,
				  struct gfs2_buffer_head **bhp);
int gfs2_dirent_first(struct gfs2_inode *dip, struct gfs2_buffer_head *bh,
					  struct gfs2_dirent **dent);
int gfs2_dirent_next(struct gfs2_inode *dip, struct gfs2_buffer_head *bh,
					 struct gfs2_dirent **dent);
void build_height(struct gfs2_inode *ip, int height);
unsigned int calc_tree_height(struct gfs2_inode *ip, uint64_t size);
void write_journal(struct gfs2_sbd *sdp, struct gfs2_inode *ip, unsigned int j,
				   unsigned int blocks);

/**
 * device_size - figure out a device's size
 * @fd: the file descriptor of a device
 * @bytes: the number of bytes the device holds
 *
 * Returns: -1 on error (with errno set), 0 on success (with @bytes set)
 */

int device_size(int fd, uint64_t *bytes);

/* locking.c */
void test_locking(char *lockproto, char *locktable);

/* gfs2_log.c */
struct gfs2_options {
	char *device;
	int yes:1;
	int no:1;
	int query:1;
};

#define MSG_DEBUG       7
#define MSG_INFO        6
#define MSG_NOTICE      5
#define MSG_WARN        4
#define MSG_ERROR       3
#define MSG_CRITICAL    2
#define MSG_NULL        1

#define print_log(iif, priority, format...)     \
do { print_fsck_log(iif, priority, __FILE__, __LINE__, ## format); } while(0)

#define log_debug(format...) \
do { print_log(0, MSG_DEBUG, format); } while(0)
#define log_info(format...) \
do { print_log(0, MSG_INFO, format); } while(0)

#define log_notice(format...) \
do { print_log(0, MSG_NOTICE, format); } while(0)

#define log_warn(format...) \
do { print_log(0, MSG_WARN, format); } while(0)

#define log_err(format...) \
do { print_log(0, MSG_ERROR, format); } while(0)

#define log_crit(format...) \
do { print_log(0, MSG_CRITICAL, format); } while(0)

#define stack log_debug("<backtrace> - %s()\n", __func__)

#define log_at_debug(format...)         \
do { print_log(1, MSG_DEBUG, format); } while(0)

#define log_at_info(format...) \
do { print_log(1, MSG_INFO, format); } while(0)

#define log_at_notice(format...) \
do { print_log(1, MSG_NOTICE, format); } while(0)

#define log_at_warn(format...) \
do { print_log(1, MSG_WARN, format); } while(0)

#define log_at_err(format...) \
do { print_log(1, MSG_ERROR, format); } while(0)

#define log_at_crit(format...) \
do { print_log(1, MSG_CRITICAL, format); } while(0)

void increase_verbosity(void);
void decrease_verbosity(void);
void print_fsck_log(int iif, int priority, char *file, int line,
					const char *format, ...);
char gfs2_getch(void);

char generic_interrupt(const char *caller, const char *where,
		       const char *progress, const char *question,
		       const char *answers);
int gfs2_query(int *setonabort, struct gfs2_options *opts,
	       const char *format, ...);

/* misc.c */
#define SYS_BASE "/sys/fs/gfs2"

void compute_constants(struct gfs2_sbd *sdp);
void check_for_gfs2(struct gfs2_sbd *sdp);
void mount_gfs2_meta(struct gfs2_sbd *sdp);
void cleanup_metafs(struct gfs2_sbd *sdp);
char *get_list(void);
char **str2lines(char *str);
char *find_debugfs_mount(void);
char *mp2fsname(char *mp);
char *name2value(char *str, char *name);
uint32_t name2u32(char *str, char *name);
uint64_t name2u64(char *str, char *name);
char *__get_sysfs(char *fsname, char *filename);
char *get_sysfs(char *fsname, char *filename);
unsigned int get_sysfs_uint(char *fsname, char *filename);
void set_sysfs(char *fsname, char *filename, char *val);
char *do_basename(char *device);
char *mp2devname(char *mp);
int is_fsname(char *name);

/* recovery.c */
void gfs2_replay_incr_blk(struct gfs2_inode *ip, unsigned int *blk);
int gfs2_replay_read_block(struct gfs2_inode *ip, unsigned int blk,
			   struct gfs2_buffer_head **bh);
int gfs2_revoke_add(struct gfs2_sbd *sdp, uint64_t blkno, unsigned int where);
int gfs2_revoke_check(struct gfs2_sbd *sdp, uint64_t blkno,
		      unsigned int where);
void gfs2_revoke_clean(struct gfs2_sbd *sdp);
int get_log_header(struct gfs2_inode *ip, unsigned int blk,
		   struct gfs2_log_header *head);
int find_good_lh(struct gfs2_inode *ip, unsigned int *blk,
		 struct gfs2_log_header *head);
int jhead_scan(struct gfs2_inode *ip, struct gfs2_log_header *head);
int gfs2_find_jhead(struct gfs2_inode *ip, struct gfs2_log_header *head);
int clean_journal(struct gfs2_inode *ip, struct gfs2_log_header *head);

/* rgrp.c */
int gfs2_compute_bitstructs(struct gfs2_sbd *sdp, struct rgrp_list *rgd);
struct rgrp_list *gfs2_blk2rgrpd(struct gfs2_sbd *sdp, uint64_t blk);
uint64_t gfs2_rgrp_read(struct gfs2_sbd *sdp, struct rgrp_list *rgd);
void gfs2_rgrp_relse(struct rgrp_list *rgd, enum update_flags updated);
void gfs2_rgrp_free(osi_list_t *rglist, enum update_flags updated);

/* structures.c */
void build_master(struct gfs2_sbd *sdp);
void build_sb(struct gfs2_sbd *sdp);
void build_jindex(struct gfs2_sbd *sdp);
void build_per_node(struct gfs2_sbd *sdp);
void build_inum(struct gfs2_sbd *sdp);
void build_statfs(struct gfs2_sbd *sdp);
void build_rindex(struct gfs2_sbd *sdp);
void build_quota(struct gfs2_sbd *sdp);
void build_root(struct gfs2_sbd *sdp);
void do_init(struct gfs2_sbd *sdp);
int gfs2_check_meta(struct gfs2_buffer_head *bh, int type);
int gfs2_set_meta(struct gfs2_buffer_head *bh, int type, int format);
int gfs2_next_rg_meta(struct rgrp_list *rgd, uint64_t *block, int first);
int gfs2_next_rg_meta_free(struct rgrp_list *rgd, uint64_t *block, int first,
						   int *mfree);
int gfs2_next_rg_metatype(struct gfs2_sbd *sdp, struct rgrp_list *rgd,
						  uint64_t *block, uint32_t type, int first);
/* super.c */
int check_sb(struct gfs2_sb *sb);
int read_sb(struct gfs2_sbd *sdp);
int ji_update(struct gfs2_sbd *sdp);
int rindex_read(struct gfs2_sbd *sdp, int fd, int *count1);
int ri_update(struct gfs2_sbd *sdp, int fd, int *rgcount);
int write_sb(struct gfs2_sbd *sdp);

/* ondisk.c */
uint32_t gfs2_disk_hash(const char *data, int len);

extern void print_it(const char *label, const char *fmt,
					 const char *fmt2, ...);
#define pv(struct, member, fmt, fmt2) do {				   \
		print_it("  "#member, fmt, fmt2, struct->member); \
        } while (FALSE);
#define pv2(struct, member, fmt, fmt2) do {		 \
		print_it("  ", fmt, fmt2, struct->member);	\
        } while (FALSE);

#endif /* __LIBGFS2_DOT_H__ */
