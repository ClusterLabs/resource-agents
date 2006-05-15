/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __LIBGFS2_DOT_H__
#define __LIBGFS2_DOT_H__

#include <inttypes.h>
#include <sys/types.h>
#include <linux/types.h>

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
#define RESRANDOM do { srandom(RANDOM(1000000000)); } while (0)
#define RANDOM(values) ((values) * (random() / (RAND_MAX + 1.0)))

/* Stolen (and must match) include/linux/fs.h: */
/* (They're not pulled in when fs.h is included from userspace */
#define DT_UNKNOWN	0
#define DT_FIFO		1
#define DT_CHR		2
#define DT_DIR		4
#define DT_BLK		6
#define DT_REG		8
#define DT_LNK		10
#define DT_SOCK		12
#define DT_WHT		14

struct subdevice {
	uint64_t start;
	uint64_t length;
	uint32_t rgf_flags;
};

struct device {
	unsigned int nsubdev;
	struct subdevice *subdev;
};

struct rgrp_list {
	osi_list_t list;

	uint32_t subdevice;	/* The subdevice who holds this resource group */

	uint64_t start;	        /* The offset of the beginning of this resource group */
	uint64_t length;	/* The length of this resource group */
	uint32_t rgf_flags;

	struct gfs2_rindex ri;
	struct gfs2_rgrp rg;
};

struct gfs2_buffer_head {
	osi_list_t b_list;
	osi_list_t b_hash;

	unsigned int b_count;
	uint64_t b_blocknr;
	char *b_data;
	unsigned int b_size;

	int b_uninit;
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

struct gfs2_sbd {
	struct gfs2_sb sd_sb;    /* a copy of the ondisk structure */
	char lockproto[GFS2_LOCKNAME_LEN];
	char locktable[GFS2_LOCKNAME_LEN];

	unsigned int bsize;	 /* The block size of the FS (in bytes) */
	unsigned int journals;
	unsigned int jsize;	 /* Size of journals (in MB) */
        unsigned int rgsize;     /* Size of resource groups (in MB) */
	unsigned int utsize;     /* Size of unlinked tag files (in MB) */
	unsigned int qcsize;     /* Size of quota change files (in MB) */

	int debug;
	int quiet;
	int test;
	int expert;
	int override;

	char *device_name;
	char *path_name;

	/* Constants */

	unsigned int bsize_shift;
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

	uint64_t next_inum;
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
	struct gfs2_inode *inum_inode;
	struct gfs2_inode *statfs_inode;
	struct gfs2_inode *root_dir;

	unsigned int spills;
	unsigned int writes;
};

extern char *prog_name;

#define GFS2_DEFAULT_BSIZE          (4096)
#define GFS2_DEFAULT_JSIZE          (32)
#define GFS2_DEFAULT_RGSIZE         (256)
#define GFS2_DEFAULT_UTSIZE         (1)
#define GFS2_DEFAULT_QCSIZE         (1)
#define GFS2_MIN_GROW_SIZE          (10)

#define DATA (1)
#define META (2)
#define DINODE (3)

/* buf.c */
struct gfs2_buffer_head *bget(struct gfs2_sbd *sdp, uint64_t num);
struct gfs2_buffer_head *bread(struct gfs2_sbd *sdp, uint64_t num);
struct gfs2_buffer_head *bhold(struct gfs2_buffer_head *bh);
void brelse(struct gfs2_buffer_head *bh);
void bsync(struct gfs2_sbd *sdp);
void write_buffer(struct gfs2_sbd *sdp, struct gfs2_buffer_head *bh);

/* device_geometry.c */
void device_geometry(struct gfs2_sbd *sdp);
void fix_device_geometry(struct gfs2_sbd *sdp);
void munge_device_geometry_for_grow(struct gfs2_sbd *sdp);

/* fs_geometry.c */
void compute_rgrp_layout(struct gfs2_sbd *sdp, int new_fs);
void build_rgrps(struct gfs2_sbd *sdp);

/* fs_ops.c */
struct gfs2_inode *inode_get(struct gfs2_sbd *sdp, struct gfs2_buffer_head *bh);
void inode_put(struct gfs2_inode *ip);
uint64_t data_alloc(struct gfs2_inode *ip);
uint64_t meta_alloc(struct gfs2_inode *ip);
uint64_t dinode_alloc(struct gfs2_sbd *sdp);
int gfs2_readi(struct gfs2_inode *ip, void *buf,
			   uint64_t offset, unsigned int size);
int gfs2_writei(struct gfs2_inode *ip, void *buf,
				uint64_t offset, unsigned int size);
struct gfs2_buffer_head *get_file_buf(struct gfs2_inode *ip, uint64_t lbn);
struct gfs2_buffer_head *init_dinode(struct gfs2_sbd *sdp, struct gfs2_inum *inum,
				unsigned int mode, uint32_t flags,
				struct gfs2_inum *parent);
struct gfs2_inode *createi(struct gfs2_inode *dip, char *filename,
			  unsigned int mode, uint32_t flags);
/* iddev.c */
/**
 * identify_device - figure out what's on a device
 * @fd: a file descriptor open on a device open for (at least) reading
 * @type: a buffer that contains the type of filesystem
 * @type_len: the amount of space pointed to by @type
 *
 * The offset of @fd will be changed by the function.
 * This routine will not write to this device.
 *
 * Returns: -1 on error (with errno set), 1 if unabled to identify,
 *          0 if device identified (with @type set)
 */

int identify_device(int fd, char *type, unsigned type_len);

/**
 * device_size - figure out a device's size
 * @fd: the file descriptor of a device
 * @bytes: the number of bytes the device holds
 *
 * Returns: -1 on error (with errno set), 0 on success (with @bytes set)
 */

int device_size(int fd, uint64_t *bytes);

/* live.c */
void check_for_gfs2(struct gfs2_sbd *sdp);
void lock_for_admin(struct gfs2_sbd *sdp);
void path2device(struct gfs2_sbd *sdp);
void find_block_size(struct gfs2_sbd *sdp);
void find_current_fssize(struct gfs2_sbd *sdp);
void add_to_rindex(struct gfs2_sbd *sdp);
void statfs_sync(struct gfs2_sbd *sdp);
void find_current_journals(struct gfs2_sbd *sdp);
int rename2system(struct gfs2_sbd *sdp, char *new_dir, char *new_name);
void make_jdata(int fd, char *value);
uint64_t bmap(int fd, uint64_t lblock);

/* locking.c */
void test_locking(char *lockproto, char *locktable);

/* misc.c */
void compute_constants(struct gfs2_sbd *sdp);

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
/* ondisk.c */
uint32_t gfs2_disk_hash(const char *data, int len);

extern void print_it(const char *label, const char *fmt,
					 const char *fmt2, ...);
#define pv(struct, member, fmt, fmt2) do {				   \
		print_it("  "#member":", fmt, fmt2, struct->member); \
        } while (FALSE);
#define pv2(struct, member, fmt, fmt2) do {		 \
		print_it("  ", fmt, fmt2, struct->member);	\
        } while (FALSE);

#endif /* __LIBGFS2_DOT_H__ */
