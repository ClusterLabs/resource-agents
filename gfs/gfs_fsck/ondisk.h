/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

/*
* NOTE:
* If you add 8 byte fields to these structures, they must be 8 byte
* aligned.  4 byte field must be 4 byte aligned, etc...
*
* All structures must be a multiple of 8 bytes long.
*
* GRIPES:
* We should have forgetten about supporting 512B FS block sizes 
* and made the di_reserved field in the struct gfs_dinode structure
* much bigger.
*
* de_rec_len in struct gfs_dirent should really have been a 32-bit value
* as it now limits us to a 64k FS block size (with the current code
* in dir.c).
*/

#ifndef __GFS_ONDISK_DOT_H__
#define __GFS_ONDISK_DOT_H__

#define GFS_MAGIC               (0x01161970)
#define GFS_BASIC_BLOCK         (512)
#define GFS_BASIC_BLOCK_SHIFT   (9)
#define GFS_DUMPS_PER_LOG       (4)

/*  Lock numbers of the LM_TYPE_NONDISK type  */

#define GFS_MOUNT_LOCK          (0)
#define GFS_LIVE_LOCK           (1)
#define GFS_TRANS_LOCK          (2)
#define GFS_RENAME_LOCK         (3)

/*  Format numbers for various metadata types  */

#define GFS_FORMAT_SB           (100)
#define GFS_FORMAT_RG           (200)
#define GFS_FORMAT_RB           (300)
#define GFS_FORMAT_DI           (400)
#define GFS_FORMAT_IN           (500)
#define GFS_FORMAT_LF           (600)
#define GFS_FORMAT_JD           (700)
#define GFS_FORMAT_LH           (800)
#define GFS_FORMAT_LD           (900)
/*  These don't have actual struct gfs_meta_header structures to go with them  */
#define GFS_FORMAT_JI           (1000)
#define GFS_FORMAT_RI           (1100)
#define GFS_FORMAT_DE           (1200)
#define GFS_FORMAT_QU           (1500)
#define GFS_FORMAT_EA           (1600)
/*  These are part of the superblock  */
#define GFS_FORMAT_FS           (1308)
#define GFS_FORMAT_MULTI        (1401)

/*
 *  An on-disk inode number
 */

#define GFS_INUM_EQUAL(ino1, ino2) \
(((ino1)->no_formal_ino == (ino2)->no_formal_ino) && \
 ((ino1)->no_addr == (ino2)->no_addr))

struct gfs_inum {
	uint64_t no_formal_ino;
	uint64_t no_addr;
};

/*
 *  Generic metadata head structure
 *
 *  Every inplace buffer logged in the journal must start with this.
 */

#define GFS_METATYPE_NONE       (0)
#define GFS_METATYPE_SB         (1)
#define GFS_METATYPE_RG         (2)
#define GFS_METATYPE_RB         (3)
#define GFS_METATYPE_DI         (4)
#define GFS_METATYPE_IN         (5)
#define GFS_METATYPE_LF         (6)
#define GFS_METATYPE_JD         (7)
#define GFS_METATYPE_LH         (8)
#define GFS_METATYPE_LD         (9)
#define GFS_METATYPE_EA         (10)

#define GFS_META_CLUMP          (64)

struct gfs_meta_header {
	uint32_t mh_magic;	/* Magic number */
	uint32_t mh_type;	/* GFS_METATYPE_XX */
	uint64_t mh_generation;	/* Generation number */
	uint32_t mh_format;	/* GFS_FORMAT_XX */
	uint32_t mh_incarn;
};

/*
 *  super-block structure
 *
 *  It's probably good if SIZEOF_SB <= GFS_BASIC_BLOCK
 */

/*  Address of SuperBlock in GFS basic blocks  */
#define GFS_SB_ADDR             (128)
/*  The lock number for the superblock (must be zero)  */
#define GFS_SB_LOCK             (0)
#define GFS_CRAP_LOCK           (1)

/*  Requirement:  GFS_LOCKNAME_LEN % 8 == 0
    Includes: the fencing zero at the end  */
#define GFS_LOCKNAME_LEN        (64)

struct gfs_sb {
	/*  Order is important  */
	struct gfs_meta_header sb_header;

	uint32_t sb_fs_format;
	uint32_t sb_multihost_format;
	uint32_t sb_flags;

	/*  Important information  */
	uint32_t sb_bsize;	/* fundamental fs block size in bytes */
	uint32_t sb_bsize_shift;	/* log2(sb_bsize) */
	uint32_t sb_seg_size;	/* Journal segment size in FS blocks */

	struct gfs_inum sb_jindex_di;	/* journal index inode number (GFS_SB_LOCK) */
	struct gfs_inum sb_rindex_di;	/* resource index inode number (GFS_SB_LOCK) */
	struct gfs_inum sb_root_di;	/* root directory inode number (GFS_ROOT_LOCK) */

	char sb_lockproto[GFS_LOCKNAME_LEN];	/* Type of locking this FS uses */
	char sb_locktable[GFS_LOCKNAME_LEN];	/* Name of lock table for this FS */

	struct gfs_inum sb_quota_di;
	struct gfs_inum sb_license_di;

	char sb_reserved[96];
};

/*
 *  journal index structure 
 */

struct gfs_jindex {
	uint64_t ji_addr;	/* starting block of the journal */
	uint32_t ji_nsegment;	/* number of segments in journal */
	uint32_t ji_pad;

	char ji_reserved[64];
};

/*
 *  resource index structure 
 */

struct gfs_rindex {
	uint64_t ri_addr;	/* rgrp block disk address */
	uint32_t ri_length;	/* length of rgrp header in fs blocks */
	uint32_t ri_pad;

	uint64_t ri_data1;	/* first data location */
	uint32_t ri_data;	/* num of data blocks in rgrp */

	uint32_t ri_bitbytes;	/* number of bytes in data bitmaps */

	char ri_reserved[64];
};

/*
 *  resource group header structure
 *
 */

/* Number of blocks per byte in rgrp */
#define GFS_NBBY                (4)
#define GFS_BIT_SIZE            (2)
#define GFS_BIT_MASK            (0x00000003)

#define GFS_BLKST_FREE          (0)
#define GFS_BLKST_USED          (1)
#define GFS_BLKST_FREEMETA      (2)
#define GFS_BLKST_USEDMETA      (3)

struct gfs_rgrp {
	struct gfs_meta_header rg_header;

	uint32_t rg_flags;	/* flags */

	uint32_t rg_free;	/* number of free data blocks */

	uint32_t rg_useddi;	/* number of dinodes */
	uint32_t rg_freedi;	/* number of unused dinodes */
	struct gfs_inum rg_freedi_list;	/* list of free dinodes */

	uint32_t rg_usedmeta;	/* number of used metadata blocks (not including dinodes) */
	uint32_t rg_freemeta;	/* number of unused metadata blocks */

	char rg_reserved[64];
};

/*
 *  Quota Structures
 */

struct gfs_quota {
	uint64_t qu_limit;
	uint64_t qu_warn;
	int64_t qu_value;

	char qu_reserved[64];
};

/*
 *  dinode structure
 */

#define GFS_MAX_META_HEIGHT     (10)
#define GFS_DIR_MAX_DEPTH       (17)

/*  Dinode types  */
#define GFS_FILE_NON            (0)
#define GFS_FILE_REG            (1)
#define GFS_FILE_DIR            (2)
#define GFS_FILE_LNK            (5)
#define GFS_FILE_BLK            (7)
#define GFS_FILE_CHR            (8)
#define GFS_FILE_FIFO           (101)
#define GFS_FILE_SOCK           (102)

/*  Dinode flags  */
#define GFS_DIF_JDATA               (0x00000001)
#define GFS_DIF_EXHASH              (0x00000002)
#define GFS_DIF_UNUSED              (0x00000004)
#define GFS_DIF_EA_INDIRECT         (0x00000008)
#define GFS_DIF_DIRECTIO            (0x00000010)
#define GFS_DIF_IMMUTABLE           (0x00000020)
#define GFS_DIF_APPENDONLY          (0x00000040)
#define GFS_DIF_NOATIME             (0x00000080)
#define GFS_DIF_SYNC                (0x00000100)
#define GFS_DIF_INHERIT_DIRECTIO    (0x40000000)
#define GFS_DIF_INHERIT_JDATA       (0x80000000)

struct gfs_dinode {
	struct gfs_meta_header di_header;

	struct gfs_inum di_num;

	uint32_t di_mode;	/* mode of file */
	uint32_t di_uid;	/* owner's user id */
	uint32_t di_gid;	/* owner's group id */
	uint32_t di_nlink;	/* number of links to this file */
	uint64_t di_size;	/* number of bytes in file */
	uint64_t di_blocks;	/* number of blocks in file */
	int64_t di_atime;	/* time last accessed */
	int64_t di_mtime;	/* time last modified */
	int64_t di_ctime;	/* time last changed */
	uint32_t di_major;	/* device major number */
	uint32_t di_minor;	/* device minor number */

	uint64_t di_rgrp;	/* dinode rgrp block number */
	uint64_t di_goal_rgrp;	/* rgrp to alloc from next */
	uint32_t di_goal_dblk;	/* data block goal */
	uint32_t di_goal_mblk;	/* metadata block goal */
	uint32_t di_flags;	/* flags */
	uint32_t di_payload_format;	/* struct gfs_rindex, struct gfs_jindex, or struct gfs_dirent */
	uint16_t di_type;	/* type of file */
	uint16_t di_height;	/* height of metadata */
	uint32_t di_incarn;	/* incarnation number */
	uint16_t di_pad;

	/*  These only apply to directories  */
	uint16_t di_depth;	/* Number of bits in the table */
	uint32_t di_entries;	/* The number of entries in the directory */

	/*  This only applies to unused inodes  */
	struct gfs_inum di_next_unused;

	uint64_t di_eattr;	/* extended attribute block number */

	char di_reserved[56];
};

/*
 *  indirect block header
 */

struct gfs_indirect {
	struct gfs_meta_header in_header;

	char in_reserved[64];
};

/*
 *  directory structure - many of these per directory file
 */

#define GFS_FNAMESIZE               (255)
#define GFS_DIRENT_SIZE(name_len) ((sizeof(struct gfs_dirent) + (name_len) + 7) & ~7)

struct gfs_dirent {
	struct gfs_inum de_inum;	/* Inode number */
	uint32_t de_hash;	/* hash of the filename */
	uint16_t de_rec_len;	/* the length of the dirent */
	uint16_t de_name_len;	/* the length of the name */
	uint16_t de_type;	/* type of dinode this points to */

	char de_reserved[14];
};

/*
 *  Header of leaf directory nodes
 */

struct gfs_leaf {
	struct gfs_meta_header lf_header;

	uint16_t lf_depth;	/* Depth of leaf */
	uint16_t lf_entries;	/* Number of dirents in leaf */
	uint32_t lf_dirent_format;	/* Format of the dirents */
	uint64_t lf_next;	/* Next leaf, if overflow */

	char lf_reserved[64];
};

/*
 *  Log header structure
 */

#define GFS_LOG_HEAD_UNMOUNT    (0x00000001)

struct gfs_log_header {
	struct gfs_meta_header lh_header;

	uint32_t lh_flags;	/* Flags */
	uint32_t lh_pad;

	uint64_t lh_first;	/* Block number of first header in this trans */
	uint64_t lh_sequence;	/* Sequence number of this transaction */

	uint64_t lh_tail;	/* Block number of log tail */
	uint64_t lh_last_dump;	/* block number of last dump */

	char lh_reserved[64];
};

/*
 *  Log type descriptor
 */

#define GFS_LOG_DESC_METADATA   (300)
/*  ld_data1 is the number of metadata blocks in the descriptor.
    ld_data2 is the transaction type.
    */

#define GFS_LOG_DESC_IUL        (400)
/*  ld_data1 is TRUE if this is a dump.
    ld_data2 is unused.
    FixMe!!!  ld_data1 should be the number of entries.
              ld_data2 should be "TRUE if this is a dump".
    */

#define GFS_LOG_DESC_IDA        (401)
/*  ld_data1 is unused.
    ld_data2 is unused.
    FixMe!!!  ld_data1 should be the number of entries.
    */

#define GFS_LOG_DESC_Q          (402)
/*  ld_data1 is the number of quota changes in the descriptor.
    ld_data2 is TRUE if this is a dump.
    */

#define GFS_LOG_DESC_LAST       (500)
/*  ld_data1 is unused.
    ld_data2 is unused.
    */

struct gfs_log_descriptor {
	struct gfs_meta_header ld_header;

	uint32_t ld_type;	/* Type of data in this log chunk */
	uint32_t ld_length;	/* Number of buffers in this chunk */
	uint32_t ld_data1;	/* descriptor specific field */
	uint32_t ld_data2;	/* descriptor specific field */

	char ld_reserved[64];
};

/*
 *  Metadata block tags
 */

struct gfs_block_tag {
	uint64_t bt_blkno;	/* inplace block number */
	uint32_t bt_flags;	/* flags */
	uint32_t bt_pad;
};

/*
 *  Quota Journal Tag
 */

#define GFS_QTF_USER            (0x00000001)

struct gfs_quota_tag {
	int64_t qt_change;
	uint32_t qt_flags;
	uint32_t qt_id;
};

/*
 *  Extended attribute header format
 */

#define GFS_EA_MAX_NAME_LEN     (255)
#define GFS_EA_MAX_DATA_LEN     (65535)

#define GFS_EATYPE_LAST	        (2)

#define GFS_EATYPE_UNUSED       (0)
#define GFS_EATYPE_USR          (1)
#define GFS_EATYPE_SYS          (2)
#define GFS_EATYPE_VALID(x)     ((x) && (x) <= GFS_EATYPE_LAST)	/* this is only
								   for requests */

#define GFS_EAFLAG_LAST         (0x01)	/* last ea in block */

struct gfs_ea_header {
	uint32_t ea_rec_len;
	uint32_t ea_data_len;
	uint8_t ea_name_len;	/* no NULL pointer after the string */
	uint8_t ea_type;	/* GFS_EATYPE_... */
	uint8_t ea_flags;
	uint8_t ea_num_ptrs;
	uint32_t ea_pad;
};

/*  Endian functions  */

#define GFS_ENDIAN_BIG

#ifdef GFS_ENDIAN_BIG

#define gfs16_to_cpu be16_to_cpu
#define gfs32_to_cpu be32_to_cpu
#define gfs64_to_cpu be64_to_cpu

#define cpu_to_gfs16 cpu_to_be16
#define cpu_to_gfs32 cpu_to_be32
#define cpu_to_gfs64 cpu_to_be64

#else				/*  GFS_ENDIAN_BIG  */

#define gfs16_to_cpu le16_to_cpu
#define gfs32_to_cpu le32_to_cpu
#define gfs64_to_cpu le64_to_cpu

#define cpu_to_gfs16 cpu_to_le16
#define cpu_to_gfs32 cpu_to_le32
#define cpu_to_gfs64 cpu_to_le64

#endif				/*  GFS_ENDIAN_BIG  */

/*  Translation functions  */

void gfs_inum_in(struct gfs_inum *no, char *buf);
void gfs_inum_out(struct gfs_inum *no, char *buf);
void gfs_meta_header_in(struct gfs_meta_header *mh, char *buf);
void gfs_meta_header_out(struct gfs_meta_header *mh, char *buf);
void gfs_sb_in(struct gfs_sb *sb, char *buf);
void gfs_sb_out(struct gfs_sb *sb, char *buf);
void gfs_jindex_in(struct gfs_jindex *jindex, char *buf);
void gfs_jindex_out(struct gfs_jindex *jindex, char *buf);
void gfs_rindex_in(struct gfs_rindex *rindex, char *buf);
void gfs_rindex_out(struct gfs_rindex *rindex, char *buf);
void gfs_rgrp_in(struct gfs_rgrp *rgrp, char *buf);
void gfs_rgrp_out(struct gfs_rgrp *rgrp, char *buf);
void gfs_quota_in(struct gfs_quota *quota, char *buf);
void gfs_quota_out(struct gfs_quota *quota, char *buf);
void gfs_dinode_in(struct gfs_dinode *dinode, char *buf);
void gfs_dinode_out(struct gfs_dinode *dinode, char *buf);
void gfs_indirect_in(struct gfs_indirect *indirect, char *buf);
void gfs_indirect_out(struct gfs_indirect *indirect, char *buf);
void gfs_dirent_in(struct gfs_dirent *dirent, char *buf);
void gfs_dirent_out(struct gfs_dirent *dirent, char *buf);
void gfs_leaf_in(struct gfs_leaf *leaf, char *buf);
void gfs_leaf_out(struct gfs_leaf *leaf, char *buf);
void gfs_log_header_in(struct gfs_log_header *head, char *buf);
void gfs_log_header_out(struct gfs_log_header *head, char *buf);
void gfs_desc_in(struct gfs_log_descriptor *desc, char *buf);
void gfs_desc_out(struct gfs_log_descriptor *desc, char *buf);
void gfs_block_tag_in(struct gfs_block_tag *btag, char *buf);
void gfs_block_tag_out(struct gfs_block_tag *btag, char *buf);
void gfs_quota_tag_in(struct gfs_quota_tag *qtag, char *buf);
void gfs_quota_tag_out(struct gfs_quota_tag *qtag, char *buf);
void gfs_ea_header_in(struct gfs_ea_header *qtag, char *buf);
void gfs_ea_header_out(struct gfs_ea_header *qtag, char *buf);

/*  Printing functions  */

void gfs_inum_print(struct gfs_inum *no, int console);
void gfs_meta_header_print(struct gfs_meta_header *mh, int console);
void gfs_sb_print(struct gfs_sb *sb, int console);
void gfs_jindex_print(struct gfs_jindex *jindex, int console);
void gfs_rindex_print(struct gfs_rindex *rindex, int console);
void gfs_rgrp_print(struct gfs_rgrp *rgrp, int console);
void gfs_quota_print(struct gfs_quota *quota, int console);
void gfs_dinode_print(struct gfs_dinode *dinode, int console);
void gfs_indirect_print(struct gfs_indirect *indirect, int console);
void gfs_dirent_print(struct gfs_dirent *dirent, char *name, int console);
void gfs_leaf_print(struct gfs_leaf *leaf, int console);
void gfs_log_header_print(struct gfs_log_header *head, int console);
void gfs_desc_print(struct gfs_log_descriptor *desc, int console);
void gfs_block_tag_print(struct gfs_block_tag *tag, int console);
void gfs_quota_tag_print(struct gfs_quota_tag *tag, int console);
void gfs_ea_header_print(struct gfs_ea_header *tag, int console);

/*  The hash function for ExHash directories  */

uint32_t gfs_dir_hash(const char *data, int len);

#endif				/*  __GFS_ONDISK_DOT_H__  */
