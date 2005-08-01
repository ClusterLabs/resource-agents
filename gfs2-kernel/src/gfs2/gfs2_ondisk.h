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

#ifndef __GFS2_ONDISK_DOT_H__
#define __GFS2_ONDISK_DOT_H__

#define GFS2_MAGIC		0x07131974
#define GFS2_BASIC_BLOCK	512
#define GFS2_BASIC_BLOCK_SHIFT	9

/* Lock numbers of the LM_TYPE_NONDISK type */

#define GFS2_MOUNT_LOCK		0
#define GFS2_LIVE_LOCK		1
#define GFS2_TRANS_LOCK		2
#define GFS2_RENAME_LOCK	3

/* Format numbers for various metadata types */

#define GFS2_FORMAT_NONE	0
#define GFS2_FORMAT_SB		100
#define GFS2_FORMAT_RG		200
#define GFS2_FORMAT_RB		300
#define GFS2_FORMAT_DI		400
#define GFS2_FORMAT_IN		500
#define GFS2_FORMAT_LF		600
#define GFS2_FORMAT_JD		700
#define GFS2_FORMAT_LH		800
#define GFS2_FORMAT_LD		900
#define GFS2_FORMAT_LB		1000
#define GFS2_FORMAT_EA		1100
#define GFS2_FORMAT_ED		1200
#define GFS2_FORMAT_UT		1300
#define GFS2_FORMAT_QC		1400
/* These are format numbers for entities contained in files */
#define GFS2_FORMAT_RI		1500
#define GFS2_FORMAT_DE		1600
#define GFS2_FORMAT_QU		1700
/* These are part of the superblock */
#define GFS2_FORMAT_FS		1801
#define GFS2_FORMAT_MULTI	1900

/*
 * An on-disk inode number
 */

#define gfs2_inum_equal(ino1, ino2) \
	(((ino1)->no_formal_ino == (ino2)->no_formal_ino) && \
	((ino1)->no_addr == (ino2)->no_addr))

struct gfs2_inum {
	uint64_t no_formal_ino;
	uint64_t no_addr;
};

/*
 * Generic metadata head structure
 * Every inplace buffer logged in the journal must start with this.
 */

#define GFS2_METATYPE_NONE	0
#define GFS2_METATYPE_SB	1
#define GFS2_METATYPE_RG	2
#define GFS2_METATYPE_RB	3
#define GFS2_METATYPE_DI	4
#define GFS2_METATYPE_IN	5
#define GFS2_METATYPE_LF	6
#define GFS2_METATYPE_JD	7
#define GFS2_METATYPE_LH	8
#define GFS2_METATYPE_LD	9
#define GFS2_METATYPE_LB	10
#define GFS2_METATYPE_EA	11
#define GFS2_METATYPE_ED	12
#define GFS2_METATYPE_UT	13
#define GFS2_METATYPE_QC	14

struct gfs2_meta_header {
	uint32_t mh_magic;
	uint16_t mh_type;
	uint16_t mh_format;
	uint64_t mh_blkno;
};

/*
 * super-block structure
 *
 * It's probably good if SIZEOF_SB <= GFS2_BASIC_BLOCK (512 bytes)
 *
 * Order is important, need to be able to read old superblocks to do on-disk
 * version upgrades.
 */

/* Address of superblock in GFS2 basic blocks */
#define GFS2_SB_ADDR		128

/* The lock number for the superblock (must be zero) */
#define GFS2_SB_LOCK		0

/* Requirement:  GFS2_LOCKNAME_LEN % 8 == 0
   Includes: the fencing zero at the end */
#define GFS2_LOCKNAME_LEN	64

struct gfs2_sb {
	struct gfs2_meta_header sb_header;

	uint32_t sb_fs_format;
	uint32_t sb_multihost_format;

	uint32_t sb_bsize;
	uint32_t sb_bsize_shift;

	struct gfs2_inum sb_master_dir;

	char sb_lockproto[GFS2_LOCKNAME_LEN];
	char sb_locktable[GFS2_LOCKNAME_LEN];
};

/*
 * resource index structure
 */

struct gfs2_rindex {
	uint64_t ri_addr;	/* grp block disk address */
	uint32_t ri_length;	/* length of rgrp header in fs blocks */
	uint32_t ri_pad;

	uint64_t ri_data0;	/* first data location */
	uint32_t ri_data;	/* num of data blocks in rgrp */

	uint32_t ri_bitbytes;	/* number of bytes in data bitmaps */

	char ri_reserved[32];
};

/*
 * resource group header structure
 */

/* Number of blocks per byte in rgrp */
#define GFS2_NBBY		4
#define GFS2_BIT_SIZE		2
#define GFS2_BIT_MASK		0x00000003

#define GFS2_BLKST_FREE		0
#define GFS2_BLKST_USED		1
#define GFS2_BLKST_INVALID	2
#define GFS2_BLKST_DINODE	3

#define GFS2_RGF_JOURNAL	0x00000001
#define GFS2_RGF_METAONLY	0x00000002
#define GFS2_RGF_DATAONLY	0x00000004
#define GFS2_RGF_NOALLOC	0x00000008

struct gfs2_rgrp {
	struct gfs2_meta_header rg_header;

	uint32_t rg_flags;
	uint32_t rg_free;
	uint32_t rg_dinodes;

	char rg_reserved[36];
};

/*
 * quota structure
 */

struct gfs2_quota {
	uint64_t qu_limit;
	uint64_t qu_warn;
	int64_t qu_value;
};

/*
 * dinode structure
 */

#define GFS2_MAX_META_HEIGHT	10
#define GFS2_DIR_MAX_DEPTH	17

#define DT2IF(dt) (((dt) << 12) & S_IFMT)
#define IF2DT(sif) (((sif) & S_IFMT) >> 12)

/* Dinode flags */
#define GFS2_DIF_SYSTEM			0x00000001
#define GFS2_DIF_JDATA			0x00000002
#define GFS2_DIF_EXHASH			0x00000004
#define GFS2_DIF_EA_INDIRECT		0x00000008
#define GFS2_DIF_DIRECTIO		0x00000010
#define GFS2_DIF_IMMUTABLE		0x00000020
#define GFS2_DIF_APPENDONLY		0x00000040
#define GFS2_DIF_NOATIME		0x00000080
#define GFS2_DIF_SYNC			0x00000100
#define GFS2_DIF_INHERIT_DIRECTIO	0x00000200
#define GFS2_DIF_INHERIT_JDATA		0x00000400
#define GFS2_DIF_TRUNC_IN_PROG		0x00000800

struct gfs2_dinode {
	struct gfs2_meta_header di_header;

	struct gfs2_inum di_num;

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

	uint64_t di_goal_meta;	/* rgrp to alloc from next */
	uint64_t di_goal_data;	/* data block goal */

	uint32_t di_flags;	/* GFS2_DIF_... */
	uint32_t di_payload_format;  /* GFS2_FORMAT_... */
	uint16_t di_height;	/* height of metadata */

	/* These only apply to directories  */
	uint16_t di_depth;	/* Number of bits in the table */
	uint32_t di_entries;	/* The number of entries in the directory */

	uint64_t di_eattr;	/* extended attribute block number */

	char di_reserved[32];
};

/*
 * directory structure - many of these per directory file
 */

#define GFS2_FNAMESIZE		255
#define GFS2_DIRENT_SIZE(name_len) ((sizeof(struct gfs2_dirent) + (name_len) + 7) & ~7)

struct gfs2_dirent {
	struct gfs2_inum de_inum;
	uint32_t de_hash;
	uint32_t de_rec_len;
	uint8_t de_name_len;
	uint8_t de_type;
	uint16_t de_pad1;
	uint32_t de_pad2;
};

/*
 * Header of leaf directory nodes
 */

struct gfs2_leaf {
	struct gfs2_meta_header lf_header;

	uint16_t lf_depth;		/* Depth of leaf */
	uint16_t lf_entries;		/* Number of dirents in leaf */
	uint32_t lf_dirent_format;	/* Format of the dirents */
	uint64_t lf_next;		/* Next leaf, if overflow */

	char lf_reserved[32];
};

/*
 * Extended attribute header format
 */

#define GFS2_EA_MAX_NAME_LEN	255
#define GFS2_EA_MAX_DATA_LEN	65536

#define GFS2_EATYPE_UNUSED	0
#define GFS2_EATYPE_USR		1
#define GFS2_EATYPE_SYS		2

#define GFS2_EATYPE_LAST	2
#define GFS2_EATYPE_VALID(x)	((x) <= GFS2_EATYPE_LAST)

#define GFS2_EAFLAG_LAST	0x01	/* last ea in block */

struct gfs2_ea_header {
	uint32_t ea_rec_len;
	uint32_t ea_data_len;
	uint8_t ea_name_len;	/* no NULL pointer after the string */
	uint8_t ea_type;	/* GFS2_EATYPE_... */
	uint8_t ea_flags;	/* GFS2_EAFLAG_... */
	uint8_t ea_num_ptrs;
	uint32_t ea_pad;
};

/*
 * Log header structure
 */

#define GFS2_LOG_HEAD_UNMOUNT	0x00000001	/* log is clean */

struct gfs2_log_header {
	struct gfs2_meta_header lh_header;

	uint64_t lh_sequence;	/* Sequence number of this transaction */
	uint32_t lh_flags;	/* GFS2_LOG_HEAD_... */
	uint32_t lh_tail;	/* Block number of log tail */
	uint32_t lh_blkno;
	uint32_t lh_hash;
};

/*
 * Log type descriptor
 */

#define GFS2_LOG_DESC_METADATA	300
/* ld_data1 is the number of metadata blocks in the descriptor.
   ld_data2 is unused. */

#define GFS2_LOG_DESC_REVOKE	301
/* ld_data1 is the number of revoke blocks in the descriptor.
   ld_data2 is unused. */

struct gfs2_log_descriptor {
	struct gfs2_meta_header ld_header;

	uint32_t ld_type;	/* GFS2_LOG_DESC_... */
	uint32_t ld_length;	/* Number of buffers in this chunk */
	uint32_t ld_data1;	/* descriptor-specific field */
	uint32_t ld_data2;	/* descriptor-specific field */

	char ld_reserved[32];
};

/*
 * Inum Range
 * Describe a range of formal inode numbers allocated to
 * one machine to assign to inodes.
 */

#define GFS2_INUM_QUANTUM	1048576

struct gfs2_inum_range {
	uint64_t ir_start;
	uint64_t ir_length;
};

/*
 * Statfs change
 * Describes an change to the pool of free and allocated
 * blocks.
 */

struct gfs2_statfs_change {
	int64_t sc_total;
	int64_t sc_free;
	int64_t sc_dinodes;
};

/*
 * Unlinked Tag
 * Describes an allocated inode that isn't linked into
 * the directory tree and might need to be deallocated.
 */

#define GFS2_UTF_UNINIT		0x00000001

struct gfs2_unlinked_tag {
	struct gfs2_inum ut_inum;
	uint32_t ut_flags;	/* GFS2_UTF_... */
	uint32_t ut_pad;
};

/*
 * Quota change
 * Describes an allocation change for a particular
 * user or group.
 */

#define GFS2_QCF_USER		0x00000001

struct gfs2_quota_change {
	int64_t qc_change;
	uint32_t qc_flags;	/* GFS2_QCF_... */
	uint32_t qc_id;
};

/* Endian functions */

#undef GFS2_ENDIAN_BIG

#ifdef GFS2_ENDIAN_BIG

#define gfs2_16_to_cpu be16_to_cpu
#define gfs2_32_to_cpu be32_to_cpu
#define gfs2_64_to_cpu be64_to_cpu

#define cpu_to_gfs2_16 cpu_to_be16
#define cpu_to_gfs2_32 cpu_to_be32
#define cpu_to_gfs2_64 cpu_to_be64

#else /* GFS2_ENDIAN_BIG */

#define gfs2_16_to_cpu le16_to_cpu
#define gfs2_32_to_cpu le32_to_cpu
#define gfs2_64_to_cpu le64_to_cpu

#define cpu_to_gfs2_16 cpu_to_le16
#define cpu_to_gfs2_32 cpu_to_le32
#define cpu_to_gfs2_64 cpu_to_le64

#endif /* GFS2_ENDIAN_BIG */

/* Translation functions */

void gfs2_inum_in(struct gfs2_inum *no, char *buf);
void gfs2_inum_out(struct gfs2_inum *no, char *buf);
void gfs2_meta_header_in(struct gfs2_meta_header *mh, char *buf);
void gfs2_meta_header_out(struct gfs2_meta_header *mh, char *buf);
void gfs2_sb_in(struct gfs2_sb *sb, char *buf);
void gfs2_sb_out(struct gfs2_sb *sb, char *buf);
void gfs2_rindex_in(struct gfs2_rindex *ri, char *buf);
void gfs2_rindex_out(struct gfs2_rindex *ri, char *buf);
void gfs2_rgrp_in(struct gfs2_rgrp *rg, char *buf);
void gfs2_rgrp_out(struct gfs2_rgrp *rg, char *buf);
void gfs2_quota_in(struct gfs2_quota *qu, char *buf);
void gfs2_quota_out(struct gfs2_quota *qu, char *buf);
void gfs2_dinode_in(struct gfs2_dinode *di, char *buf);
void gfs2_dinode_out(struct gfs2_dinode *di, char *buf);
void gfs2_dirent_in(struct gfs2_dirent *de, char *buf);
void gfs2_dirent_out(struct gfs2_dirent *de, char *buf);
void gfs2_leaf_in(struct gfs2_leaf *lf, char *buf);
void gfs2_leaf_out(struct gfs2_leaf *lf, char *buf);
void gfs2_ea_header_in(struct gfs2_ea_header *ea, char *buf);
void gfs2_ea_header_out(struct gfs2_ea_header *ea, char *buf);
void gfs2_log_header_in(struct gfs2_log_header *lh, char *buf);
void gfs2_log_header_out(struct gfs2_log_header *lh, char *buf);
void gfs2_log_descriptor_in(struct gfs2_log_descriptor *ld, char *buf);
void gfs2_log_descriptor_out(struct gfs2_log_descriptor *ld, char *buf);
void gfs2_inum_range_in(struct gfs2_inum_range *ir, char *buf);
void gfs2_inum_range_out(struct gfs2_inum_range *ir, char *buf);
void gfs2_statfs_change_in(struct gfs2_statfs_change *sc, char *buf);
void gfs2_statfs_change_out(struct gfs2_statfs_change *sc, char *buf);
void gfs2_unlinked_tag_in(struct gfs2_unlinked_tag *ut, char *buf);
void gfs2_unlinked_tag_out(struct gfs2_unlinked_tag *ut, char *buf);
void gfs2_quota_change_in(struct gfs2_quota_change *qc, char *buf);
void gfs2_quota_change_out(struct gfs2_quota_change *qc, char *buf);

/* Printing functions */

void gfs2_inum_print(struct gfs2_inum *no);
void gfs2_meta_header_print(struct gfs2_meta_header *mh);
void gfs2_sb_print(struct gfs2_sb *sb);
void gfs2_rindex_print(struct gfs2_rindex *ri);
void gfs2_rgrp_print(struct gfs2_rgrp *rg);
void gfs2_quota_print(struct gfs2_quota *qu);
void gfs2_dinode_print(struct gfs2_dinode *di);
void gfs2_dirent_print(struct gfs2_dirent *de, char *name);
void gfs2_leaf_print(struct gfs2_leaf *lf);
void gfs2_ea_header_print(struct gfs2_ea_header *ea, char *name);
void gfs2_log_header_print(struct gfs2_log_header *lh);
void gfs2_log_descriptor_print(struct gfs2_log_descriptor *ld);
void gfs2_inum_range_print(struct gfs2_inum_range *ir);
void gfs2_statfs_change_print(struct gfs2_statfs_change *sc);
void gfs2_unlinked_tag_print(struct gfs2_unlinked_tag *ut);
void gfs2_quota_change_print(struct gfs2_quota_change *qc);

/* The hash function for ExHash directories and
   crcs of commit blocks */

uint32_t gfs2_disk_hash(const char *data, int len);

#endif /* __GFS2_ONDISK_DOT_H__ */



#ifdef WANT_GFS2_CONVERSION_FUNCTIONS

#define CPIN_08(s1, s2, member, count) {memcpy((s1->member), (s2->member), (count));}
#define CPOUT_08(s1, s2, member, count) {memcpy((s2->member), (s1->member), (count));}
#define CPIN_16(s1, s2, member) {(s1->member) = gfs2_16_to_cpu((s2->member));}
#define CPOUT_16(s1, s2, member) {(s2->member) = cpu_to_gfs2_16((s1->member));}
#define CPIN_32(s1, s2, member) {(s1->member) = gfs2_32_to_cpu((s2->member));}
#define CPOUT_32(s1, s2, member) {(s2->member) = cpu_to_gfs2_32((s1->member));}
#define CPIN_64(s1, s2, member) {(s1->member) = gfs2_64_to_cpu((s2->member));}
#define CPOUT_64(s1, s2, member) {(s2->member) = cpu_to_gfs2_64((s1->member));}

#define pv(struct, member, fmt) printk("  "#member" = "fmt"\n", struct->member);
#define pa(struct, member, count) print_array(#member, struct->member, count);

/**
 * print_array - Print out an array of bytes
 * @title: what to print before the array
 * @buf: the array
 * @count: the number of bytes
 *
 */

static void print_array(char *title, char *buf, int count)
{
	int x;

	printk("  %s =\n", title);
	for (x = 0; x < count; x++) {
		printk("%.2X ", (unsigned char)buf[x]);
		if (x % 16 == 15)
			printk("\n");
	}
	if (x % 16)
		printk("\n");
}

/**
 * gfs2_inum_in - Read in an inode number
 * @no: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_inum_in(struct gfs2_inum *no, char *buf)
{
	struct gfs2_inum *str = (struct gfs2_inum *)buf;

	CPIN_64(no, str, no_formal_ino);
	CPIN_64(no, str, no_addr);
}

/**
 * gfs2_inum_out - Write out an inode number
 * @no: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_inum_out(struct gfs2_inum *no, char *buf)
{
	struct gfs2_inum *str = (struct gfs2_inum *)buf;

	CPOUT_64(no, str, no_formal_ino);
	CPOUT_64(no, str, no_addr);
}

/**
 * gfs2_inum_print - Print out a inode number
 * @no: the cpu-order buffer
 *
 */

void gfs2_inum_print(struct gfs2_inum *no)
{
	pv(no, no_formal_ino, "%"PRIu64);
	pv(no, no_addr, "%"PRIu64);
}

/**
 * gfs2_meta_header_in - Read in a metadata header
 * @mh: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_meta_header_in(struct gfs2_meta_header *mh, char *buf)
{
	struct gfs2_meta_header *str = (struct gfs2_meta_header *)buf;

	CPIN_32(mh, str, mh_magic);
	CPIN_16(mh, str, mh_type);
	CPIN_16(mh, str, mh_format);
	CPIN_64(mh, str, mh_blkno);
}

/**
 * gfs2_meta_header_in - Write out a metadata header
 * @mh: the cpu-order structure
 * @buf: the disk-order buffer
 *
 * Don't ever change the generation number in this routine.
 * It's done manually in increment_generation().
 */

void gfs2_meta_header_out(struct gfs2_meta_header *mh, char *buf)
{
	struct gfs2_meta_header *str = (struct gfs2_meta_header *)buf;

	CPOUT_32(mh, str, mh_magic);
	CPOUT_16(mh, str, mh_type);
	CPOUT_16(mh, str, mh_format);
	CPOUT_64(mh, str, mh_blkno);
}

/**
 * gfs2_meta_header_print - Print out a metadata header
 * @mh: the cpu-order buffer
 *
 */

void gfs2_meta_header_print(struct gfs2_meta_header *mh)
{
	pv(mh, mh_magic, "0x%.8X");
	pv(mh, mh_type, "%u");
	pv(mh, mh_format, "%u");
	pv(mh, mh_blkno, "%"PRIu64);
}

/**
 * gfs2_sb_in - Read in a superblock
 * @sb: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_sb_in(struct gfs2_sb *sb, char *buf)
{
	struct gfs2_sb *str = (struct gfs2_sb *)buf;

	gfs2_meta_header_in(&sb->sb_header, buf);

	CPIN_32(sb, str, sb_fs_format);
	CPIN_32(sb, str, sb_multihost_format);

	CPIN_32(sb, str, sb_bsize);
	CPIN_32(sb, str, sb_bsize_shift);

	gfs2_inum_in(&sb->sb_master_dir, (char *)&str->sb_master_dir);

	CPIN_08(sb, str, sb_lockproto, GFS2_LOCKNAME_LEN);
	CPIN_08(sb, str, sb_locktable, GFS2_LOCKNAME_LEN);
}

/**
 * gfs2_sb_out - Write out a superblock
 * @sb: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_sb_out(struct gfs2_sb *sb, char *buf)
{
	struct gfs2_sb *str = (struct gfs2_sb *)buf;

	gfs2_meta_header_out(&sb->sb_header, buf);

	CPOUT_32(sb, str, sb_fs_format);
	CPOUT_32(sb, str, sb_multihost_format);

	CPOUT_32(sb, str, sb_bsize);
	CPOUT_32(sb, str, sb_bsize_shift);

	gfs2_inum_out(&sb->sb_master_dir, (char *)&str->sb_master_dir);

	CPOUT_08(sb, str, sb_lockproto, GFS2_LOCKNAME_LEN);
	CPOUT_08(sb, str, sb_locktable, GFS2_LOCKNAME_LEN);
}

/**
 * gfs2_sb_print - Print out a superblock
 * @sb: the cpu-order buffer
 *
 */

void gfs2_sb_print(struct gfs2_sb *sb)
{
	gfs2_meta_header_print(&sb->sb_header);

	pv(sb, sb_fs_format, "%u");
	pv(sb, sb_multihost_format, "%u");

	pv(sb, sb_bsize, "%u");
	pv(sb, sb_bsize_shift, "%u");

	gfs2_inum_print(&sb->sb_master_dir);

	pv(sb, sb_lockproto, "%s");
	pv(sb, sb_locktable, "%s");
}

/**
 * gfs2_rindex_in - Read in a resource index structure
 * @ri: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_rindex_in(struct gfs2_rindex *ri, char *buf)
{
	struct gfs2_rindex *str = (struct gfs2_rindex *)buf;

	CPIN_64(ri, str, ri_addr);
	CPIN_32(ri, str, ri_length);
	CPIN_32(ri, str, ri_pad);

	CPIN_64(ri, str, ri_data0);
	CPIN_32(ri, str, ri_data);

	CPIN_32(ri, str, ri_bitbytes);

	CPIN_08(ri, str, ri_reserved, 32);
}

/**
 * gfs2_rindex_out - Write out a resource index structure
 * @ri: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_rindex_out(struct gfs2_rindex *ri, char *buf)
{
	struct gfs2_rindex *str = (struct gfs2_rindex *)buf;

	CPOUT_64(ri, str, ri_addr);
	CPOUT_32(ri, str, ri_length);
	CPOUT_32(ri, str, ri_pad);

	CPOUT_64(ri, str, ri_data0);
	CPOUT_32(ri, str, ri_data);

	CPOUT_32(ri, str, ri_bitbytes);

	CPOUT_08(ri, str, ri_reserved, 32);
}

/**
 * gfs2_rindex_print - Print out a resource index structure
 * @ri: the cpu-order buffer
 *
 */

void gfs2_rindex_print(struct gfs2_rindex *ri)
{
	pv(ri, ri_addr, "%"PRIu64);
	pv(ri, ri_length, "%u");
	pv(ri, ri_pad, "%u");

	pv(ri, ri_data0, "%"PRIu64);
	pv(ri, ri_data, "%u");

	pv(ri, ri_bitbytes, "%u");

	pa(ri, ri_reserved, 32);
}

/**
 * gfs2_rgrp_in - Read in a resource group header
 * @rg: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_rgrp_in(struct gfs2_rgrp *rg, char *buf)
{
	struct gfs2_rgrp *str = (struct gfs2_rgrp *)buf;

	gfs2_meta_header_in(&rg->rg_header, buf);
	CPIN_32(rg, str, rg_flags);
	CPIN_32(rg, str, rg_free);
	CPIN_32(rg, str, rg_dinodes);

	CPIN_08(rg, str, rg_reserved, 36);
}

/**
 * gfs2_rgrp_out - Write out a resource group header
 * @rg: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_rgrp_out(struct gfs2_rgrp *rg, char *buf)
{
	struct gfs2_rgrp *str = (struct gfs2_rgrp *)buf;

	gfs2_meta_header_out(&rg->rg_header, buf);
	CPOUT_32(rg, str, rg_flags);
	CPOUT_32(rg, str, rg_free);
	CPOUT_32(rg, str, rg_dinodes);

	CPOUT_08(rg, str, rg_reserved, 36);
}

/**
 * gfs2_rgrp_print - Print out a resource group header
 * @rg: the cpu-order buffer
 *
 */

void gfs2_rgrp_print(struct gfs2_rgrp *rg)
{
	gfs2_meta_header_print(&rg->rg_header);
	pv(rg, rg_flags, "%u");
	pv(rg, rg_free, "%u");
	pv(rg, rg_dinodes, "%u");

	pa(rg, rg_reserved, 36);
}

/**
 * gfs2_quota_in - Read in a qu structures
 * @qu: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_quota_in(struct gfs2_quota *qu, char *buf)
{
	struct gfs2_quota *str = (struct gfs2_quota *)buf;

	CPIN_64(qu, str, qu_limit);
	CPIN_64(qu, str, qu_warn);
	CPIN_64(qu, str, qu_value);
}

/**
 * gfs2_quota_out - Write out a qu structure
 * @qu: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_quota_out(struct gfs2_quota *qu, char *buf)
{
	struct gfs2_quota *str = (struct gfs2_quota *)buf;

	CPOUT_64(qu, str, qu_limit);
	CPOUT_64(qu, str, qu_warn);
	CPOUT_64(qu, str, qu_value);
}

/**
 * gfs2_quota_print - Print out a qu structure
 * @qu: the cpu-order buffer
 *
 */

void gfs2_quota_print(struct gfs2_quota *qu)
{
	pv(qu, qu_limit, "%"PRIu64);
	pv(qu, qu_warn, "%"PRIu64);
	pv(qu, qu_value, "%"PRId64);
}

/**
 * gfs2_dinode_in - Read in a di
 * @di: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_dinode_in(struct gfs2_dinode *di, char *buf)
{
	struct gfs2_dinode *str = (struct gfs2_dinode *)buf;

	gfs2_meta_header_in(&di->di_header, buf);
	gfs2_inum_in(&di->di_num, (char *)&str->di_num);

	CPIN_32(di, str, di_mode);
	CPIN_32(di, str, di_uid);
	CPIN_32(di, str, di_gid);
	CPIN_32(di, str, di_nlink);
	CPIN_64(di, str, di_size);
	CPIN_64(di, str, di_blocks);
	CPIN_64(di, str, di_atime);
	CPIN_64(di, str, di_mtime);
	CPIN_64(di, str, di_ctime);
	CPIN_32(di, str, di_major);
	CPIN_32(di, str, di_minor);

	CPIN_64(di, str, di_goal_meta);
	CPIN_64(di, str, di_goal_data);

	CPIN_32(di, str, di_flags);
	CPIN_32(di, str, di_payload_format);
	CPIN_16(di, str, di_height);

	CPIN_16(di, str, di_depth);
	CPIN_32(di, str, di_entries);

	CPIN_64(di, str, di_eattr);

	CPIN_08(di, str, di_reserved, 32);
}

/**
 * gfs2_dinode_out - Write out a di
 * @di: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_dinode_out(struct gfs2_dinode *di, char *buf)
{
	struct gfs2_dinode *str = (struct gfs2_dinode *)buf;

	gfs2_meta_header_out(&di->di_header, buf);
	gfs2_inum_out(&di->di_num, (char *)&str->di_num);

	CPOUT_32(di, str, di_mode);
	CPOUT_32(di, str, di_uid);
	CPOUT_32(di, str, di_gid);
	CPOUT_32(di, str, di_nlink);
	CPOUT_64(di, str, di_size);
	CPOUT_64(di, str, di_blocks);
	CPOUT_64(di, str, di_atime);
	CPOUT_64(di, str, di_mtime);
	CPOUT_64(di, str, di_ctime);
	CPOUT_32(di, str, di_major);
	CPOUT_32(di, str, di_minor);

	CPOUT_64(di, str, di_goal_meta);
	CPOUT_64(di, str, di_goal_data);

	CPOUT_32(di, str, di_flags);
	CPOUT_32(di, str, di_payload_format);
	CPOUT_16(di, str, di_height);

	CPOUT_16(di, str, di_depth);
	CPOUT_32(di, str, di_entries);

	CPOUT_64(di, str, di_eattr);

	CPOUT_08(di, str, di_reserved, 32);
}

/**
 * gfs2_dinode_print - Print out a di
 * @di: the cpu-order buffer
 *
 */

void gfs2_dinode_print(struct gfs2_dinode *di)
{
	gfs2_meta_header_print(&di->di_header);
	gfs2_inum_print(&di->di_num);

	pv(di, di_mode, "0%o");
	pv(di, di_uid, "%u");
	pv(di, di_gid, "%u");
	pv(di, di_nlink, "%u");
	pv(di, di_size, "%"PRIu64);
	pv(di, di_blocks, "%"PRIu64);
	pv(di, di_atime, "%"PRId64);
	pv(di, di_mtime, "%"PRId64);
	pv(di, di_ctime, "%"PRId64);
	pv(di, di_major, "%u");
	pv(di, di_minor, "%u");

	pv(di, di_goal_meta, "%"PRIu64);
	pv(di, di_goal_data, "%"PRIu64);

	pv(di, di_flags, "0x%.8X");
	pv(di, di_payload_format, "%u");
	pv(di, di_height, "%u");

	pv(di, di_depth, "%u");
	pv(di, di_entries, "%u");

	pv(di, di_eattr, "%"PRIu64);

	pa(di, di_reserved, 32);
}

/**
 * gfs2_dirent_in - Read in a directory entry
 * @de: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_dirent_in(struct gfs2_dirent *de, char *buf)
{
	struct gfs2_dirent *str = (struct gfs2_dirent *)buf;

	gfs2_inum_in(&de->de_inum, buf);
	CPIN_32(de, str, de_hash);
	CPIN_32(de, str, de_rec_len);
	de->de_name_len = str->de_name_len;
	de->de_type = str->de_type;
	CPIN_16(de, str, de_pad1);
	CPIN_32(de, str, de_pad2);
}

/**
 * gfs2_dirent_out - Write out a directory entry
 * @de: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_dirent_out(struct gfs2_dirent *de, char *buf)
{
	struct gfs2_dirent *str = (struct gfs2_dirent *)buf;

	gfs2_inum_out(&de->de_inum, buf);
	CPOUT_32(de, str, de_hash);
	CPOUT_32(de, str, de_rec_len);
	str->de_name_len = de->de_name_len;
	str->de_type = de->de_type;
	CPOUT_16(de, str, de_pad1);
	CPOUT_32(de, str, de_pad2);
}

/**
 * gfs2_dirent_print - Print out a directory entry
 * @de: the cpu-order buffer
 * @name: the filename
 *
 */

void gfs2_dirent_print(struct gfs2_dirent *de, char *name)
{
	char buf[GFS2_FNAMESIZE + 1];

	gfs2_inum_print(&de->de_inum);
	pv(de, de_hash, "0x%.8X");
	pv(de, de_rec_len, "%u");
	pv(de, de_name_len, "%u");
	pv(de, de_type, "%u");
	pv(de, de_pad1, "%u");
	pv(de, de_pad2, "%u");

	memset(buf, 0, GFS2_FNAMESIZE + 1);
	memcpy(buf, name, de->de_name_len);
	printk("  name = %s\n", buf);
}

/**
 * gfs2_leaf_in - Read in a directory lf header
 * @lf: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_leaf_in(struct gfs2_leaf *lf, char *buf)
{
	struct gfs2_leaf *str = (struct gfs2_leaf *)buf;

	gfs2_meta_header_in(&lf->lf_header, buf);
	CPIN_16(lf, str, lf_depth);
	CPIN_16(lf, str, lf_entries);
	CPIN_32(lf, str, lf_dirent_format);
	CPIN_64(lf, str, lf_next);

	CPIN_08(lf, str, lf_reserved, 32);
}

/**
 * gfs2_leaf_out - Write out a directory lf header
 * @lf: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_leaf_out(struct gfs2_leaf *lf, char *buf)
{
	struct gfs2_leaf *str = (struct gfs2_leaf *)buf;

	gfs2_meta_header_out(&lf->lf_header, buf);
	CPOUT_16(lf, str, lf_depth);
	CPOUT_16(lf, str, lf_entries);
	CPOUT_32(lf, str, lf_dirent_format);
	CPOUT_64(lf, str, lf_next);

	CPOUT_08(lf, str, lf_reserved, 32);
}

/**
 * gfs2_leaf_print - Print out a directory lf header
 * @lf: the cpu-order buffer
 *
 */

void gfs2_leaf_print(struct gfs2_leaf *lf)
{
	gfs2_meta_header_print(&lf->lf_header);
	pv(lf, lf_depth, "%u");
	pv(lf, lf_entries, "%u");
	pv(lf, lf_dirent_format, "%u");
	pv(lf, lf_next, "%"PRIu64);

	pa(lf, lf_reserved, 32);
}

/**
 * gfs2_ea_header_in - Read in a Extended Attribute header
 * @qc: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_ea_header_in(struct gfs2_ea_header *ea, char *buf)
{
	struct gfs2_ea_header *str = (struct gfs2_ea_header *)buf;

	CPIN_32(ea, str, ea_rec_len);
	CPIN_32(ea, str, ea_data_len);
	ea->ea_name_len = str->ea_name_len;
	ea->ea_type = str->ea_type;
	ea->ea_flags = str->ea_flags;
	ea->ea_num_ptrs = str->ea_num_ptrs;
	CPIN_32(ea, str, ea_pad);
}

/**
 * gfs2_ea_header_out - Write out a Extended Attribute header
 * @ea: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_ea_header_out(struct gfs2_ea_header *ea, char *buf)
{
	struct gfs2_ea_header *str = (struct gfs2_ea_header *)buf;

	CPOUT_32(ea, str, ea_rec_len);
	CPOUT_32(ea, str, ea_data_len);
	str->ea_name_len = ea->ea_name_len;
	str->ea_type = ea->ea_type;
	str->ea_flags = ea->ea_flags;
	str->ea_num_ptrs = ea->ea_num_ptrs;
	CPOUT_32(ea, str, ea_pad);
}

/**
 * gfs2_ea_header_print - Print out a Extended Attribute header
 * @ea: the cpu-order buffer
 *
 */

void gfs2_ea_header_print(struct gfs2_ea_header *ea, char *name)
{
	char buf[GFS2_EA_MAX_NAME_LEN + 1];

	pv(ea, ea_rec_len, "%u");
	pv(ea, ea_data_len, "%u");
	pv(ea, ea_name_len, "%u");
	pv(ea, ea_type, "%u");
	pv(ea, ea_flags, "%u");
	pv(ea, ea_num_ptrs, "%u");
	pv(ea, ea_pad, "%u");

	memset(buf, 0, GFS2_EA_MAX_NAME_LEN + 1);
	memcpy(buf, name, ea->ea_name_len);
	printk("  name = %s\n", buf);
}

/**
 * gfs2_log_header_in - Read in a log header
 * @lh: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_log_header_in(struct gfs2_log_header *lh, char *buf)
{
	struct gfs2_log_header *str = (struct gfs2_log_header *)buf;

	gfs2_meta_header_in(&lh->lh_header, buf);
	CPIN_64(lh, str, lh_sequence);
	CPIN_32(lh, str, lh_flags);
	CPIN_32(lh, str, lh_tail);
	CPIN_32(lh, str, lh_blkno);
	CPIN_32(lh, str, lh_hash);
}

/**
 * gfs2_log_header_out - Write out a log header
 * @lh: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_log_header_out(struct gfs2_log_header *lh, char *buf)
{
	struct gfs2_log_header *str = (struct gfs2_log_header *)buf;

	gfs2_meta_header_out(&lh->lh_header, buf);
	CPOUT_64(lh, str, lh_sequence);
	CPOUT_32(lh, str, lh_flags);
	CPOUT_32(lh, str, lh_tail);
	CPOUT_32(lh, str, lh_blkno);
	CPOUT_32(lh, str, lh_hash);
}

/**
 * gfs2_log_header_print - Print out a log header
 * @lh: the cpu-order buffer
 *
 */

void gfs2_log_header_print(struct gfs2_log_header *lh)
{
	gfs2_meta_header_print(&lh->lh_header);
	pv(lh, lh_sequence, "%"PRIu64);
	pv(lh, lh_flags, "0x%.8X");
	pv(lh, lh_tail, "%u");
	pv(lh, lh_blkno, "%u");
	pv(lh, lh_hash, "0x%.8X");
}

/**
 * gfs2_log_descriptor_in - Read in a log descriptor
 * @ld: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_log_descriptor_in(struct gfs2_log_descriptor *ld, char *buf)
{
	struct gfs2_log_descriptor *str = (struct gfs2_log_descriptor *)buf;

	gfs2_meta_header_in(&ld->ld_header, buf);
	CPIN_32(ld, str, ld_type);
	CPIN_32(ld, str, ld_length);
	CPIN_32(ld, str, ld_data1);
	CPIN_32(ld, str, ld_data2);

	CPIN_08(ld, str, ld_reserved, 32);
}

/**
 * gfs2_log_descriptor_out - Write out a log descriptor
 * @ld: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_log_descriptor_out(struct gfs2_log_descriptor *ld, char *buf)
{
	struct gfs2_log_descriptor *str = (struct gfs2_log_descriptor *)buf;

	gfs2_meta_header_out(&ld->ld_header, buf);
	CPOUT_32(ld, str, ld_type);
	CPOUT_32(ld, str, ld_length);
	CPOUT_32(ld, str, ld_data1);
	CPOUT_32(ld, str, ld_data2);

	CPOUT_08(ld, str, ld_reserved, 32);
}

/**
 * gfs2_log_descriptor_print - Print out a log descriptor
 * @ld: the cpu-order buffer
 *
 */

void gfs2_log_descriptor_print(struct gfs2_log_descriptor *ld)
{
	gfs2_meta_header_print(&ld->ld_header);
	pv(ld, ld_type, "%u");
	pv(ld, ld_length, "%u");
	pv(ld, ld_data1, "%u");
	pv(ld, ld_data2, "%u");

	pa(ld, ld_reserved, 32);
}

/**
 * gfs2_inum_range_in - Read in an inum range
 * @ir: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_inum_range_in(struct gfs2_inum_range *ir, char *buf)
{
	struct gfs2_inum_range *str = (struct gfs2_inum_range *)buf;

	CPIN_64(ir, str, ir_start);
	CPIN_64(ir, str, ir_length);
}

/**
 * gfs2_inum_range_out - Write out an inum range
 * @ir: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_inum_range_out(struct gfs2_inum_range *ir, char *buf)
{
	struct gfs2_inum_range *str = (struct gfs2_inum_range *)buf;

	CPOUT_64(ir, str, ir_start);
	CPOUT_64(ir, str, ir_length);
}

/**
 * gfs2_inum_range_print - Print out an inum range
 * @ir: the cpu-order structure
 *
 */

void gfs2_inum_range_print(struct gfs2_inum_range *ir)
{
	pv(ir, ir_start, "%"PRIu64);
	pv(ir, ir_length, "%"PRIu64);
}

/**
 * gfs2_statfs_change_in - Read in a statfs change
 * @sc: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_statfs_change_in(struct gfs2_statfs_change *sc, char *buf)
{
	struct gfs2_statfs_change *str = (struct gfs2_statfs_change *)buf;

	CPIN_64(sc, str, sc_total);
	CPIN_64(sc, str, sc_free);
	CPIN_64(sc, str, sc_dinodes);
}

/**
 * gfs2_statfs_change_out - Write out a statfs change
 * @sc: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_statfs_change_out(struct gfs2_statfs_change *sc, char *buf)
{
	struct gfs2_statfs_change *str = (struct gfs2_statfs_change *)buf;

	CPOUT_64(sc, str, sc_total);
	CPOUT_64(sc, str, sc_free);
	CPOUT_64(sc, str, sc_dinodes);
}

/**
 * gfs2_statfs_change_print - Print out a quota change
 * @sc: the cpu-order buffer
 *
 */

void gfs2_statfs_change_print(struct gfs2_statfs_change *sc)
{
	pv(sc, sc_total, "%"PRId64);
	pv(sc, sc_free, "%"PRId64);
	pv(sc, sc_dinodes, "%"PRId64);
}

/**
 * gfs2_unlinked_tag_in - Read in an unlinked tag
 * @ut: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_unlinked_tag_in(struct gfs2_unlinked_tag *ut, char *buf)
{
	struct gfs2_unlinked_tag *str = (struct gfs2_unlinked_tag *)buf;

	gfs2_inum_in(&ut->ut_inum, buf);
	CPIN_32(ut, str, ut_flags);
	CPIN_32(ut, str, ut_pad);
}

/**
 * gfs2_unlinked_tag_out - Write out an unlinked tag
 * @ut: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_unlinked_tag_out(struct gfs2_unlinked_tag *ut, char *buf)
{
	struct gfs2_unlinked_tag *str = (struct gfs2_unlinked_tag *)buf;

	gfs2_inum_out(&ut->ut_inum, buf);
	CPOUT_32(ut, str, ut_flags);
	CPOUT_32(ut, str, ut_pad);
}

/**
 * gfs2_unlinked_tag_print - Print out an unlinked tag
 * @ut: the cpu-order structure
 *
 */

void gfs2_unlinked_tag_print(struct gfs2_unlinked_tag *ut)
{
	gfs2_inum_print(&ut->ut_inum);
	pv(ut, ut_flags, "%u");
	pv(ut, ut_pad, "%u");
}

/**
 * gfs2_quota_change_in - Read in a quota change
 * @qc: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_quota_change_in(struct gfs2_quota_change *qc, char *buf)
{
	struct gfs2_quota_change *str = (struct gfs2_quota_change *)buf;

	CPIN_64(qc, str, qc_change);
	CPIN_32(qc, str, qc_flags);
	CPIN_32(qc, str, qc_id);
}

/**
 * gfs2_quota_change_out - Write out a quota change
 * @qc: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void gfs2_quota_change_out(struct gfs2_quota_change *qc, char *buf)
{
	struct gfs2_quota_change *str = (struct gfs2_quota_change *)buf;

	CPOUT_64(qc, str, qc_change);
	CPOUT_32(qc, str, qc_flags);
	CPOUT_32(qc, str, qc_id);
}

/**
 * gfs2_quota_change_print - Print out a quota change
 * @qc: the cpu-order buffer
 *
 */

void gfs2_quota_change_print(struct gfs2_quota_change *qc)
{
	pv(qc, qc_change, "%"PRId64);
	pv(qc, qc_flags, "0x%.8X");
	pv(qc, qc_id, "%u");
}

static const uint32_t crc_32_tab[] =
{
  0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
  0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
  0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
  0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
  0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
  0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
  0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
  0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
  0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
  0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
  0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
  0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
  0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
  0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
  0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
  0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
  0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
  0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
  0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
  0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
  0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
  0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
  0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236, 0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
  0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
  0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
  0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
  0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
  0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
  0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
  0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
  0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
  0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

/**
 * gfs2_disk_hash - hash an array of data
 * @data: the data to be hashed
 * @len: the length of data to be hashed
 *
 * Take some data and convert it to a 32-bit hash.
 *
 * The hash function is a 32-bit CRC of the data.  The algorithm uses
 * the crc_32_tab table above.
 *
 * This may not be the fastest hash function, but it does a fair bit better
 * at providing uniform results than the others I've looked at.  That's
 * really important for efficient directories.
 *
 * Returns: the hash
 */

uint32_t gfs2_disk_hash(const char *data, int len)
{
	uint32_t hash = 0xFFFFFFFF;

	for (; len--; data++)
		hash = crc_32_tab[(hash ^ *data) & 0xFF] ^ (hash >> 8);

	hash = ~hash;

	return hash;
}

#endif /* WANT_GFS2_CONVERSION_FUNCTIONS */

