/*
 * On-disk structures.
 *
 * THE BIG PICTURE of on-disk layout:
 *
 * GFS filesystem code views the entire filesystem, including journals, as
 * one contiguous group of blocks on one (perhaps virtual) storage device.
 * The filesystem space is shared, not distributed; each node in the cluster
 * must see the entire filesystem space.
 *
 * If the filesystem is spread across multiple physical storage devices,
 * volume management (device mapping) must be used to present the fileystem
 * space to GFS as one (virtual) device, with contiguous blocks.
 *
 * The superblock contains basic information about the filesytem, and appears
 * at a location 64 KBytes into the filesystem.  The first 64 KBytes of the
 * filesystem are empty, providing a safety buffer against wayward volume
 * management software (that sometimes write data into the first few bytes of
 * a device) or administrators.
 *
 * After the superblock, the rest of the filesystem is divided into multiple
 * Resource Groups and several journals.
 *
 * The Resource Groups (RGs or rgrps) contain the allocatable blocks that are
 * used for storing files, directories, etc., and all of the associated
 * metadata.  Each RG has its own set of block allocation statistics (within
 * the RG header), a number of blocks containing the block allocation bitmap,
 * and a large number of allocatable blocks for file data and metadata.
 * Multiple RGs allow multiple nodes to simultaneously allocate blocks from the 
 * filesystem (using different RGs), enhancing parallel access.  RG size and
 * number of RGs are determined by gfs_mkfs when creating the filesystem.
 * An administrator can specify RG size (see man gfs_mkfs).
 *
 * The journals contain temporary copies of metadata blocks, along with
 * other data, that allow GFS to recover the filesystem to a consistent state
 * (at least as far as metadata is concerned) if a node fails in the midst
 * of performing a write transaction.  There must be one journal for each node
 * in the cluster.  Since access to the entire filesystem space is shared,
 * if a node crashes, another node will be able to read the crashed node's
 * journal, and perform recovery.
 *
 * Currently, gfs_mkfs places the journals right in the middle of a freshly
 * created filesystem space, between 2 large groups of RGs.  From a filesystem
 * layout perspective, this placement is not a requirement; the journals
 * could be placed anywhere within the filesystem space.
 *
 * New Resource Groups and Journals may be added to the filesystem after the
 * filesystem has been created, if the filesystem's (virtual) device is made
 * larger.  See man gfs_grow and gfs_jadd.
 *
 * A few special hidden inodes are contained in a GFS filesystem.  They do
 * not appear in any directories; instead, the superblock points to them
 * using block numbers for their location.  The special inodes are:
 *
 *   Root inode:  Root directory of the filesystem
 *   Resource Group Index:  A file containing block numbers and sizes of all RGs
 *   Journal Index:  A file containing block numbers and sizes of all journals
 *   Quota:  A file containing all quota information for the filesystem
 *   License:  A file containing license information
 *
 * Note that there is NOTHING RELATED TO INTER-NODE LOCK MANAGEMENT ON-DISK.
 * Locking is handled completely off-disk, typically via LAN.
 *
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

#define GFS_MAGIC               (0x01161970) /* for all on-disk headers */
#define GFS_BASIC_BLOCK         (512)  /* "basic block" = "sector" = 512B */
#define GFS_BASIC_BLOCK_SHIFT   (9)

/*  Controls how much data can be logged in-core before dumping log to disk */

#define GFS_DUMPS_PER_LOG       (4)    /* 1/4 of on-disk journal size*/

/*  Lock numbers of the LM_TYPE_NONDISK type.  These protect certain
 *  cluster-wide operations (rather than on-disk entities).
 *  Currently, the LIVE lock is not used for any real purpose.  */

#define GFS_MOUNT_LOCK          (0)    /* only one node can Mount at a time */
#define GFS_LIVE_LOCK           (1)    /* shared by all mounted nodes */
#define GFS_TRANS_LOCK          (2)    /* Transaction, protects jrnl recovery */
#define GFS_RENAME_LOCK         (3)    /* only one node can Rename at a time */

/*  On-disk format (version) numbers for various metadata types,
 *  used in gfs_meta_header  */

#define GFS_FORMAT_SB           (100)  /* Super-Block */
#define GFS_FORMAT_RG           (200)  /* Resource Group Header */
#define GFS_FORMAT_RB           (300)  /* Resource Group Block Alloc BitBlock */
#define GFS_FORMAT_DI           (400)  /* "Disk" inode (dinode) */
#define GFS_FORMAT_IN           (500)  /* Indirect dinode block list */
#define GFS_FORMAT_LF           (600)  /* Leaf dinode block list */
#define GFS_FORMAT_JD           (700)  /* Journal Data */
#define GFS_FORMAT_LH           (800)  /* Log Header */
#define GFS_FORMAT_LD           (900)  /* Log Descriptor */
/*  These don't have actual struct gfs_meta_header structures to go with them */
#define GFS_FORMAT_JI           (1000) /* Journal Index */
#define GFS_FORMAT_RI           (1100) /* Resource Group Index */
#define GFS_FORMAT_DE           (1200) /* Directory Entry */
#define GFS_FORMAT_QU           (1500) /* Quota */
#define GFS_FORMAT_EA           (1600) /* Extended Attribute */
#define GFS_FORMAT_ED           (1700) /* Extended Attribute data */
/*  These version #s are embedded in the superblock  */
#define GFS_FORMAT_FS           (1309) /* Filesystem (all-encompassing) */
#define GFS_FORMAT_MULTI        (1401) /* Multi-Host */

/*
 *  An on-disk inode number
 *  Initially, the on-disk block address of the inode block is assigned as the
 *  formal (permanent) ID as well.  Block address can change (to move inode
 *  on-disk), but formal ID must stay unchanged once assigned.
 */

#define gfs_inum_equal(ino1, ino2) \
(((ino1)->no_formal_ino == (ino2)->no_formal_ino) && \
 ((ino1)->no_addr == (ino2)->no_addr))

struct gfs_inum {
	uint64_t no_formal_ino;        /* inode identifier */
	uint64_t no_addr;              /* block # of dinode block */
};

/*
 *  Generic metadata head structure
 *
 *  Every inplace buffer logged in the journal must start
 *  with a struct gfs_meta_header.
 *
 *  In addition to telling what kind of metadata is in the block,
 *  the metaheader contains the important generation and incarnation
 *  numbers.
 *
 *  The generation number is used during journal recovery to determine
 *  whether an in-place block on-disk is older than an on-disk journaled copy
 *  of the block.  If so, GFS overwrites the in-place block with the journaled
 *  version of the block.
 *
 *  A meta block's generation number must increment monotonically across the
 *  cluster, each time new contents are committed to the block.  This means
 *  that whenever GFS allocates a pre-existing metadata block, GFS must read
 *  that block from disk (in case another node has incremented it).  It also
 *  means that GFS must sync the block (with incremented generation number)
 *  to disk (both log and in-place blocks), not only after changing contents
 *  of the block, but also after de-allocating the block (GFS can't just throw
 *  away incore metadata for a file that it's just erased).
 *
 *  The incarnation number is used only for on-disk (d)inodes.  GFS increments
 *  it each time it de-allocates a dinode block (i.e. each time the dinode
 *  loses its identity with a particular file, directory, etc.).  When the
 *  dinode is later allocated (i.e. to be identified with a new file, etc.),
 *  GFS copies the incarnation number into the VFS inode's i_generation member.
 *  If GFS is used as the backing store for an NFS server, GFS uses this
 *  i_generation number as part of the NFS filehandle, which differentiates
 *  it from the previous identity of the dinode, and helps protect against
 *  filesystem corruption that could happen with the use of outdated,
 *  invalid, or malicious filehandles.  See ops_export.c.
 *
 *  GFS caches de-allocated meta-headers, to minimize disk reads.
 *  See struct gfs_meta_header_cache.
 */

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

#define GFS_META_CLUMP          (64)   /* # blocks to convert fm data to meta */

struct gfs_meta_header {
	uint32_t mh_magic;      /* GFS_MAGIC sanity check magic number */
	uint32_t mh_type;       /* GFS_METATYPE_XX type of metadata block */
	uint64_t mh_generation; /* increment before writing to journal */
	uint32_t mh_format;     /* GFS_FORMAT_XX (version # for this type) */
	uint32_t mh_incarn;     /* increment when marking dinode "unused" */
};

/*
 *  super-block structure
 *
 *  One of these is at beginning of filesystem.
 *  It's probably good if SIZEOF_SB <= GFS_BASIC_BLOCK (512 bytes)
 */

/*  Address of SuperBlock in GFS basic blocks.  1st 64K of filesystem is empty
    for safety against getting clobbered by wayward volume managers, etc.
    64k was chosen because it's the largest GFS-supported fs block size.  */
#define GFS_SB_ADDR             (128)

/*  The lock number for the superblock (must be zero)  */
#define GFS_SB_LOCK             (0)
#define GFS_CRAP_LOCK           (1)

/*  Requirement:  GFS_LOCKNAME_LEN % 8 == 0
    Includes: the fencing zero at the end  */
#define GFS_LOCKNAME_LEN        (64)

struct gfs_sb {
	/*  Order is important; need to be able to read old superblocks
	    in order to support on-disk version upgrades */
	struct gfs_meta_header sb_header;

	uint32_t sb_fs_format;         /* GFS_FORMAT_FS (on-disk version) */
	uint32_t sb_multihost_format;  /* GFS_FORMAT_MULTI */
	uint32_t sb_flags;             /* ?? */

	uint32_t sb_bsize;             /* fundamental FS block size in bytes */
	uint32_t sb_bsize_shift;       /* log2(sb_bsize) */
	uint32_t sb_seg_size;          /* Journal segment size in FS blocks */

	/* These special inodes do not appear in any on-disk directory. */
	struct gfs_inum sb_jindex_di;  /* journal index inode */
	struct gfs_inum sb_rindex_di;  /* resource group index inode */
	struct gfs_inum sb_root_di;    /* root directory inode */

	/* Default inter-node locking protocol (lock module) and namespace */
	char sb_lockproto[GFS_LOCKNAME_LEN]; /* lock protocol name */
	char sb_locktable[GFS_LOCKNAME_LEN]; /* unique name for this FS */

	/* More special inodes */
	struct gfs_inum sb_quota_di;   /* quota inode */
	struct gfs_inum sb_license_di; /* license inode */

	char sb_reserved[96];
};

/*
 *  journal index structure 
 *
 *  One for each journal used by the filesystem.
 *  These descriptors are packed contiguously within the jindex inode (file).
 */

struct gfs_jindex {
	uint64_t ji_addr;       /* starting block of the journal */
	uint32_t ji_nsegment;   /* number (quantity) of segments in journal */
	uint32_t ji_pad;

	char ji_reserved[64];
};

/*
 *  resource index structure 
 *
 *  One of these for each resource group in the filesystem.
 *  These descriptors are packed contiguously within the rindex inode (file).
 *  Also see struct gfs_rgrp.
 */

struct gfs_rindex {
	uint64_t ri_addr;     /* block # of 1st block (header) in rgrp */
	uint32_t ri_length;   /* # fs blocks containing rgrp header & bitmap */
	uint32_t ri_pad;

	uint64_t ri_data1;    /* block # of first data/meta block in rgrp */
	uint32_t ri_data;     /* number (qty) of data/meta blocks in rgrp */

	uint32_t ri_bitbytes; /* total # bytes used by block alloc bitmap */

	char ri_reserved[64];
};

/*
 *  resource group header structure
 *
 *  One of these at beginning of the first block of an rgrp,
 *     followed by block alloc bitmap data in remainder of first block.
 *  Each resource group contains:
 *    Header block, including block allocation statistics (struct gfs_rgrp)
 *       and first part of block alloc bitmap.
 *    Bitmap block(s), continuing block alloc bitmap started in header block.
 *    Data/meta blocks, allocatable blocks containing file data and metadata.
 *  
 *  In older versions, now-unused (but previously allocated) dinodes were
 *  saved for re-use in an on-disk linked list (chain).  This is no longer
 *  done, but support still exists for reclaiming dinodes from this list,
 *  to support upgrades from older on-disk formats.
 */

/* Each data block within rgrp is represented by 2 bits in the alloc bitmap */
#define GFS_NBBY                (4)  /* # blocks represented by 1 bitmap byte */
#define GFS_BIT_SIZE            (2)
#define GFS_BIT_MASK            (0x00000003)

/*
 * 4 possible block allocation states:
 *   bit 0 = alloc(1)/free(0)
 *   bit 1 = metadata(1)/data(0)
 */
#define GFS_BLKST_FREE          (0)
#define GFS_BLKST_USED          (1)
#define GFS_BLKST_FREEMETA      (2)
#define GFS_BLKST_USEDMETA      (3)

struct gfs_rgrp {
	struct gfs_meta_header rg_header;

	uint32_t rg_flags;      /* ?? */

	uint32_t rg_free;       /* Number (qty) of free data blocks */

	/* Dinodes are USEDMETA, but are handled separately from other METAs */
	uint32_t rg_useddi;     /* Number (qty) of dinodes (used or free) */
	uint32_t rg_freedi;     /* Number (qty) of unused (free) dinodes */
	struct gfs_inum rg_freedi_list; /* 1st block in chain of free dinodes */

	/* These META statistics do not include dinodes (used or free) */
	uint32_t rg_usedmeta;   /* Number (qty) of used metadata blocks */
	uint32_t rg_freemeta;   /* Number (qty) of unused metadata blocks */

	char rg_reserved[64];
};

/*
 *  quota structure
 */

struct gfs_quota {
	uint64_t qu_limit;
	uint64_t qu_warn;
	int64_t qu_value;

	char qu_reserved[64];
};

/*
 *  dinode (disk inode) structure
 *  The ondisk representation of inodes
 *  One for each file, directory, etc.
 *  GFS does not put more than one inode in a single block.
 *  The inode may be "stuffed", carrying file data along with metadata,
 *    if the file data is small enough.
 *  Otherwise, the inode block contains pointers to other blocks that contain
 *    either file data or other pointers to other blocks (indirect addressing
 *    via a metadata tree).
 */

#define GFS_MAX_META_HEIGHT     (10)
#define GFS_DIR_MAX_DEPTH       (17)

/*  Dinode types  */
#define GFS_FILE_NON            (0)
#define GFS_FILE_REG            (1)    /* regular file */
#define GFS_FILE_DIR            (2)    /* directory */
#define GFS_FILE_LNK            (5)    /* link */
#define GFS_FILE_BLK            (7)    /* block device node */
#define GFS_FILE_CHR            (8)    /* character device node */
#define GFS_FILE_FIFO           (101)  /* fifo/pipe */
#define GFS_FILE_SOCK           (102)  /* socket */

/*  Dinode flags  */
#define GFS_DIF_JDATA             (0x00000001) /* jrnl all data for this file */
#define GFS_DIF_EXHASH            (0x00000002) /* hashed directory (leaves) */
#define GFS_DIF_UNUSED            (0x00000004) /* unused dinode */
#define GFS_DIF_EA_INDIRECT       (0x00000008) /* extended attribute, indirect*/
#define GFS_DIF_DIRECTIO          (0x00000010)
#define GFS_DIF_IMMUTABLE         (0x00000020) /* Can't change file */
#define GFS_DIF_APPENDONLY        (0x00000040) /* Can only add to end of file */
#define GFS_DIF_NOATIME           (0x00000080) /* Don't update access time
						  (currently unused/ignored) */
#define GFS_DIF_SYNC              (0x00000100) /* Flush to disk, don't cache
						  (currently unused/ignored) */
#define GFS_DIF_INHERIT_DIRECTIO  (0x40000000) /* new files get DIRECTIO flag */
#define GFS_DIF_INHERIT_JDATA     (0x80000000) /* new files get JDATA flag */

struct gfs_dinode {
	struct gfs_meta_header di_header;

	struct gfs_inum di_num; /* formal inode # and block address */

	uint32_t di_mode;	/* mode of file */
	uint32_t di_uid;	/* owner's user id */
	uint32_t di_gid;	/* owner's group id */
	uint32_t di_nlink;	/* number (qty) of links to this file */
	uint64_t di_size;	/* number (qty) of bytes in file */
	uint64_t di_blocks;	/* number (qty) of blocks in file */
	int64_t di_atime;	/* time last accessed */
	int64_t di_mtime;	/* time last modified */
	int64_t di_ctime;	/* time last changed */

	/*  Non-zero only for character or block device nodes  */
	uint32_t di_major;	/* device major number */
	uint32_t di_minor;	/* device minor number */

	/*  Block allocation strategy  */
	uint64_t di_rgrp;	/* dinode rgrp block number */
	uint64_t di_goal_rgrp;	/* rgrp to alloc from next */
	uint32_t di_goal_dblk;	/* data block goal */
	uint32_t di_goal_mblk;	/* metadata block goal */

	uint32_t di_flags;	/* GFS_DIF_... */

	/*  struct gfs_rindex, struct gfs_jindex, or struct gfs_dirent */
	uint32_t di_payload_format;  /* GFS_FORMAT_... */
	uint16_t di_type;	/* GFS_FILE_... type of file */
	uint16_t di_height;	/* height of metadata (0 == stuffed) */
	uint32_t di_incarn;	/* incarnation (unused, see gfs_meta_header) */
	uint16_t di_pad;

	/*  These only apply to directories  */
	uint16_t di_depth;	/* Number of bits in the table */
	uint32_t di_entries;	/* The # (qty) of entries in the directory */

	/*  This formed an on-disk chain of unused dinodes  */
	struct gfs_inum di_next_unused;  /* used in old versions only */

	uint64_t di_eattr;	/* extended attribute block number */

	char di_reserved[56];
};

/*
 *  indirect block header
 *
 *  A component of a dinode's indirect addressing metadata tree.
 *  These are pointed to by pointers in dinodes or other indirect blocks.
 */

struct gfs_indirect {
	struct gfs_meta_header in_header;

	char in_reserved[64];
};

/*
 *  directory structure - many of these per directory file
 *
 * See comments at beginning of dir.c
 */

#define GFS_FNAMESIZE               (255)
#define GFS_DIRENT_SIZE(name_len) ((sizeof(struct gfs_dirent) + (name_len) + 7) & ~7)
#define IS_LEAF     (1) /* Hashed (leaf) directory */
#define IS_DINODE   (2) /* Linear (stuffed dinode block) directory */

struct gfs_dirent {
	struct gfs_inum de_inum;    /* formal inode number and block address */
	uint32_t de_hash;           /* hash of the filename */
	uint16_t de_rec_len;        /* the length of the dirent */
	uint16_t de_name_len;       /* the length of the name */
	uint16_t de_type;           /* GFS_FILE_... type of dinode this points to */

	char de_reserved[14];
};

/*
 *  Header of leaf directory nodes
 *
 * See comments at beginning of dir.c
 */

struct gfs_leaf {
	struct gfs_meta_header lf_header;

	uint16_t lf_depth;          /* Depth of leaf */
	uint16_t lf_entries;        /* Number of dirents in leaf */
	uint32_t lf_dirent_format;  /* GFS_FORMAT_DE (version #) */
	uint64_t lf_next;           /* Next leaf, if overflow */

	char lf_reserved[64];
};

/*
 *  Log header structure
 *
 *  Two of these are in the first block of a transaction log:
 *    1)  at beginning of block
 *    2)  at end of first 512-byte sector within block
 */

#define GFS_LOG_HEAD_UNMOUNT    (0x00000001)  /* log is clean, can unmount fs */

struct gfs_log_header {
	struct gfs_meta_header lh_header;

	uint32_t lh_flags;	/* GFS_LOG_HEAD_... */
	uint32_t lh_pad;

	uint64_t lh_first;	/* Block number of first header in this trans */
	uint64_t lh_sequence;	/* Sequence number of this transaction */

	uint64_t lh_tail;	/* Block number of log tail */
	uint64_t lh_last_dump;	/* Block number of last dump */

	char lh_reserved[64];
};

/*
 *  Log type descriptor
 *
 *  One of these for each chunk in a transaction
 */

#define GFS_LOG_DESC_METADATA   (300)    /* metadata */
/*  ld_data1 is the number (quantity) of metadata blocks in the descriptor.
    ld_data2 is unused.
    */

#define GFS_LOG_DESC_IUL        (400)    /* unlinked inode */
/*  ld_data1 is TRUE if this is a dump.
    ld_data2 is unused.
    FixMe!!!  ld_data1 should be the number (quantity) of entries.
              ld_data2 should be "TRUE if this is a dump".
    */

#define GFS_LOG_DESC_IDA        (401)    /* de-allocated inode */
/*  ld_data1 is unused.
    ld_data2 is unused.
    FixMe!!!  ld_data1 should be the number (quantity) of entries.
    */

#define GFS_LOG_DESC_Q          (402)    /* quota */
/*  ld_data1 is the number of quota changes in the descriptor.
    ld_data2 is TRUE if this is a dump.
    */

#define GFS_LOG_DESC_LAST       (500)    /* final in a logged transaction */
/*  ld_data1 is unused.
    ld_data2 is unused.
    */

struct gfs_log_descriptor {
	struct gfs_meta_header ld_header;

	uint32_t ld_type;	/* GFS_LOG_DESC_... Type of this log chunk */
	uint32_t ld_length;	/* Number of buffers in this chunk */
	uint32_t ld_data1;	/* descriptor-specific field */
	uint32_t ld_data2;	/* descriptor-specific field */

	char ld_reserved[64];
};

/*
 *  Metadata block tags
 *
 *  One for each logged block.  Tells where block really belongs on-disk.
 *  These descriptor tags are packed contiguously after a gfs_log_descriptor.
 */

struct gfs_block_tag {
	uint64_t bt_blkno;	/* inplace block number */
	uint32_t bt_flags;	/* ?? */
	uint32_t bt_pad;
};

/*
 *  Quota Journal Tag
 */

#define GFS_QTF_USER            (0x00000001)

struct gfs_quota_tag {
	int64_t qt_change;
	uint32_t qt_flags;      /* GFS_QTF_... */
	uint32_t qt_id;
};

/*
 *  Extended attribute header format
 */

#define GFS_EA_MAX_NAME_LEN     (255)
#define GFS_EA_MAX_DATA_LEN     (65536)

#define GFS_EATYPE_UNUSED       (0)
#define GFS_EATYPE_USR          (1)     /* user attribute */
#define GFS_EATYPE_SYS          (2)     /* system attribute */
#define GFS_EATYPE_SECURITY	(3)	/* security attribute */

#define GFS_EATYPE_LAST         (3)
#define GFS_EATYPE_VALID(x)     ((x) <= GFS_EATYPE_LAST)

#define GFS_EAFLAG_LAST         (0x01)	/* last ea in block */

struct gfs_ea_header {
	uint32_t ea_rec_len;    /* total record length: hdr + name + data */
	uint32_t ea_data_len;   /* data length, in bytes */
	uint8_t ea_name_len;    /* no NULL pointer after the string */
	uint8_t ea_type;        /* GFS_EATYPE_... */
	uint8_t ea_flags;       /* GFS_EAFLAG_... */
	uint8_t ea_num_ptrs;    /* # fs blocks needed for EA */
	uint32_t ea_pad;
};

/*
 * Statfs change
 * Describes an change to the pool of free and allocated
 * blocks.
 */

struct gfs_statfs_change {
	uint64_t sc_total;
	uint64_t sc_free;
	uint64_t sc_dinodes;
};

struct gfs_statfs_change_host {
	int64_t sc_total;
	int64_t sc_free;
	int64_t sc_dinodes;
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

void gfs_inum_print(struct gfs_inum *no);
void gfs_meta_header_print(struct gfs_meta_header *mh);
void gfs_sb_print(struct gfs_sb *sb);
void gfs_jindex_print(struct gfs_jindex *jindex);
void gfs_rindex_print(struct gfs_rindex *rindex);
void gfs_rgrp_print(struct gfs_rgrp *rgrp);
void gfs_quota_print(struct gfs_quota *quota);
void gfs_dinode_print(struct gfs_dinode *dinode);
void gfs_indirect_print(struct gfs_indirect *indirect);
void gfs_dirent_print(struct gfs_dirent *dirent, char *name);
void gfs_leaf_print(struct gfs_leaf *leaf);
void gfs_log_header_print(struct gfs_log_header *head);
void gfs_desc_print(struct gfs_log_descriptor *desc);
void gfs_block_tag_print(struct gfs_block_tag *tag);
void gfs_quota_tag_print(struct gfs_quota_tag *tag);
void gfs_ea_header_print(struct gfs_ea_header *ea, char *name);

/*  The hash function for ExHash directories  */

uint32_t gfs_dir_hash(const char *data, int len);

#endif /* __GFS_ONDISK_DOT_H__ */



#ifdef WANT_GFS_CONVERSION_FUNCTIONS

#define CPIN_08(s1, s2, member, count) {memcpy((s1->member), (s2->member), (count));}
#define CPOUT_08(s1, s2, member, count) {memcpy((s2->member), (s1->member), (count));}
#define CPIN_16(s1, s2, member) {(s1->member) = gfs16_to_cpu((s2->member));}
#define CPOUT_16(s1, s2, member) {(s2->member) = cpu_to_gfs16((s1->member));}
#define CPIN_32(s1, s2, member) {(s1->member) = gfs32_to_cpu((s2->member));}
#define CPOUT_32(s1, s2, member) {(s2->member) = cpu_to_gfs32((s1->member));}
#define CPIN_64(s1, s2, member) {(s1->member) = gfs64_to_cpu((s2->member));}
#define CPOUT_64(s1, s2, member) {(s2->member) = cpu_to_gfs64((s1->member));}

#define pa(struct, member, count) print_array(#member, struct->member, count);

/**
 * print_array - Print out an array of bytes
 * @title: what to print before the array
 * @buf: the array
 * @count: the number of bytes
 *
 */

static void
print_array(char *title, char *buf, int count)
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
 * gfs_inum_in - Read in an inode number
 * @no: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_inum_in(struct gfs_inum *no, char *buf)
{
	struct gfs_inum *str = (struct gfs_inum *)buf;

	CPIN_64(no, str, no_formal_ino);
	CPIN_64(no, str, no_addr);
}

/**
 * gfs_inum_out - Write out an inode number
 * @no: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_inum_out(struct gfs_inum *no, char *buf)
{
	struct gfs_inum *str = (struct gfs_inum *)buf;

	CPOUT_64(no, str, no_formal_ino);
	CPOUT_64(no, str, no_addr);
}

/**
 * gfs_inum_print - Print out a inode number
 * @no: the cpu-order buffer
 *
 */

void
gfs_inum_print(struct gfs_inum *no)
{
	pv(no, no_formal_ino, "%"PRIu64);
	pv(no, no_addr, "%"PRIu64);
}

/**
 * gfs_meta_header_in - Read in a metadata header
 * @mh: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_meta_header_in(struct gfs_meta_header *mh, char *buf)
{
	struct gfs_meta_header *str = (struct gfs_meta_header *)buf;

	CPIN_32(mh, str, mh_magic);
	CPIN_32(mh, str, mh_type);
	CPIN_64(mh, str, mh_generation);
	CPIN_32(mh, str, mh_format);
	CPIN_32(mh, str, mh_incarn);
}

/**
 * gfs_meta_header_in - Write out a metadata header
 * @mh: the cpu-order structure
 * @buf: the disk-order buffer
 *
 * Don't ever change the generation number in this routine.
 * It's done manually in increment_generation().
 */

void
gfs_meta_header_out(struct gfs_meta_header *mh, char *buf)
{
	struct gfs_meta_header *str = (struct gfs_meta_header *)buf;

	CPOUT_32(mh, str, mh_magic);
	CPOUT_32(mh, str, mh_type);
#if 0
	/* Don't do this!
	   Mh_generation should only be change manually. */
	CPOUT_64(mh, str, mh_generation);
#endif
	CPOUT_32(mh, str, mh_format);
	CPOUT_32(mh, str, mh_incarn);
}

/**
 * gfs_meta_header_print - Print out a metadata header
 * @mh: the cpu-order buffer
 *
 */

void
gfs_meta_header_print(struct gfs_meta_header *mh)
{
	pv(mh, mh_magic, "0x%.8X");
	pv(mh, mh_type, "%u");
	pv(mh, mh_generation, "%"PRIu64);
	pv(mh, mh_format, "%u");
	pv(mh, mh_incarn, "%u");
}

/**
 * gfs_sb_in - Read in a superblock
 * @sb: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_sb_in(struct gfs_sb *sb, char *buf)
{
	struct gfs_sb *str = (struct gfs_sb *)buf;

	gfs_meta_header_in(&sb->sb_header, buf);

	CPIN_32(sb, str, sb_fs_format);
	CPIN_32(sb, str, sb_multihost_format);
	CPIN_32(sb, str, sb_flags);

	CPIN_32(sb, str, sb_bsize);
	CPIN_32(sb, str, sb_bsize_shift);
	CPIN_32(sb, str, sb_seg_size);

	gfs_inum_in(&sb->sb_jindex_di, (char *)&str->sb_jindex_di);
	gfs_inum_in(&sb->sb_rindex_di, (char *)&str->sb_rindex_di);
	gfs_inum_in(&sb->sb_root_di, (char *)&str->sb_root_di);

	CPIN_08(sb, str, sb_lockproto, GFS_LOCKNAME_LEN);
	CPIN_08(sb, str, sb_locktable, GFS_LOCKNAME_LEN);

	gfs_inum_in(&sb->sb_quota_di, (char *)&str->sb_quota_di);
	gfs_inum_in(&sb->sb_license_di, (char *)&str->sb_license_di);

	CPIN_08(sb, str, sb_reserved, 96);
}

/**
 * gfs_sb_out - Write out a superblock
 * @sb: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_sb_out(struct gfs_sb *sb, char *buf)
{
	struct gfs_sb *str = (struct gfs_sb *)buf;

	gfs_meta_header_out(&sb->sb_header, buf);

	CPOUT_32(sb, str, sb_fs_format);
	CPOUT_32(sb, str, sb_multihost_format);
	CPOUT_32(sb, str, sb_flags);

	CPOUT_32(sb, str, sb_bsize);
	CPOUT_32(sb, str, sb_bsize_shift);
	CPOUT_32(sb, str, sb_seg_size);

	gfs_inum_out(&sb->sb_jindex_di, (char *)&str->sb_jindex_di);
	gfs_inum_out(&sb->sb_rindex_di, (char *)&str->sb_rindex_di);
	gfs_inum_out(&sb->sb_root_di, (char *)&str->sb_root_di);

	CPOUT_08(sb, str, sb_lockproto, GFS_LOCKNAME_LEN);
	CPOUT_08(sb, str, sb_locktable, GFS_LOCKNAME_LEN);

	gfs_inum_out(&sb->sb_quota_di, (char *)&str->sb_quota_di);
	gfs_inum_out(&sb->sb_license_di, (char *)&str->sb_license_di);

	CPOUT_08(sb, str, sb_reserved, 96);
}

/**
 * gfs_sb_print - Print out a superblock
 * @sb: the cpu-order buffer
 *
 */

void
gfs_sb_print(struct gfs_sb *sb)
{
	gfs_meta_header_print(&sb->sb_header);

	pv(sb, sb_fs_format, "%u");
	pv(sb, sb_multihost_format, "%u");
	pv(sb, sb_flags, "%u");

	pv(sb, sb_bsize, "%u");
	pv(sb, sb_bsize_shift, "%u");
	pv(sb, sb_seg_size, "%u");

	gfs_inum_print(&sb->sb_jindex_di);
	gfs_inum_print(&sb->sb_rindex_di);
	gfs_inum_print(&sb->sb_root_di);

	pv(sb, sb_lockproto, "%s");
	pv(sb, sb_locktable, "%s");

	gfs_inum_print(&sb->sb_quota_di);
	gfs_inum_print(&sb->sb_license_di);

	pa(sb, sb_reserved, 96);
}

/**
 * gfs_jindex_in - Read in a journal index structure
 * @jindex: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_jindex_in(struct gfs_jindex *jindex, char *buf)
{
	struct gfs_jindex *str = (struct gfs_jindex *)buf;

	CPIN_64(jindex, str, ji_addr);
	CPIN_32(jindex, str, ji_nsegment);
	CPIN_32(jindex, str, ji_pad);

	CPIN_08(jindex, str, ji_reserved, 64);
}

/**
 * gfs_jindex_out - Write out a journal index structure
 * @jindex: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_jindex_out(struct gfs_jindex *jindex, char *buf)
{
	struct gfs_jindex *str = (struct gfs_jindex *)buf;

	CPOUT_64(jindex, str, ji_addr);
	CPOUT_32(jindex, str, ji_nsegment);
	CPOUT_32(jindex, str, ji_pad);

	CPOUT_08(jindex, str, ji_reserved, 64);
}

/**
 * gfs_jindex_print - Print out a journal index structure
 * @ji: the cpu-order buffer
 *
 */

void
gfs_jindex_print(struct gfs_jindex *ji)
{
	pv(ji, ji_addr, "%"PRIu64);
	pv(ji, ji_nsegment, "%u");
	pv(ji, ji_pad, "%u");

	pa(ji, ji_reserved, 64);
}

/**
 * gfs_rindex_in - Read in a resource index structure
 * @rindex: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_rindex_in(struct gfs_rindex *rindex, char *buf)
{
	struct gfs_rindex *str = (struct gfs_rindex *)buf;

	CPIN_64(rindex, str, ri_addr);
	CPIN_32(rindex, str, ri_length);
	CPIN_32(rindex, str, ri_pad);

	CPIN_64(rindex, str, ri_data1);
	CPIN_32(rindex, str, ri_data);

	CPIN_32(rindex, str, ri_bitbytes);

	CPIN_08(rindex, str, ri_reserved, 64);
}

/**
 * gfs_rindex_out - Write out a resource index structure
 * @rindex: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_rindex_out(struct gfs_rindex *rindex, char *buf)
{
	struct gfs_rindex *str = (struct gfs_rindex *)buf;

	CPOUT_64(rindex, str, ri_addr);
	CPOUT_32(rindex, str, ri_length);
	CPOUT_32(rindex, str, ri_pad);

	CPOUT_64(rindex, str, ri_data1);
	CPOUT_32(rindex, str, ri_data);

	CPOUT_32(rindex, str, ri_bitbytes);

	CPOUT_08(rindex, str, ri_reserved, 64);
}

/**
 * gfs_rindex_print - Print out a resource index structure
 * @ri: the cpu-order buffer
 *
 */

void
gfs_rindex_print(struct gfs_rindex *ri)
{
	pv(ri, ri_addr, "%"PRIu64);
	pv(ri, ri_length, "%u");
	pv(ri, ri_pad, "%u");

	pv(ri, ri_data1, "%"PRIu64);
	pv(ri, ri_data, "%u");

	pv(ri, ri_bitbytes, "%u");

	pa(ri, ri_reserved, 64);
}

/**
 * gfs_rgrp_in - Read in a resource group header
 * @rgrp: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_rgrp_in(struct gfs_rgrp *rgrp, char *buf)
{
	struct gfs_rgrp *str = (struct gfs_rgrp *)buf;

	gfs_meta_header_in(&rgrp->rg_header, buf);

	CPIN_32(rgrp, str, rg_flags);

	CPIN_32(rgrp, str, rg_free);

	CPIN_32(rgrp, str, rg_useddi);
	CPIN_32(rgrp, str, rg_freedi);
	gfs_inum_in(&rgrp->rg_freedi_list, (char *)&str->rg_freedi_list);

	CPIN_32(rgrp, str, rg_usedmeta);
	CPIN_32(rgrp, str, rg_freemeta);

	CPIN_08(rgrp, str, rg_reserved, 64);
}

/**
 * gfs_rgrp_out - Write out a resource group header
 * @rgrp: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_rgrp_out(struct gfs_rgrp *rgrp, char *buf)
{
	struct gfs_rgrp *str = (struct gfs_rgrp *)buf;

	gfs_meta_header_out(&rgrp->rg_header, buf);

	CPOUT_32(rgrp, str, rg_flags);

	CPOUT_32(rgrp, str, rg_free);

	CPOUT_32(rgrp, str, rg_useddi);
	CPOUT_32(rgrp, str, rg_freedi);
	gfs_inum_out(&rgrp->rg_freedi_list, (char *)&str->rg_freedi_list);

	CPOUT_32(rgrp, str, rg_usedmeta);
	CPOUT_32(rgrp, str, rg_freemeta);

	CPOUT_08(rgrp, str, rg_reserved, 64);
}

/**
 * gfs_rgrp_print - Print out a resource group header
 * @rg: the cpu-order buffer
 *
 */

void
gfs_rgrp_print(struct gfs_rgrp *rg)
{
	gfs_meta_header_print(&rg->rg_header);

	pv(rg, rg_flags, "%u");

	pv(rg, rg_free, "%u");

	pv(rg, rg_useddi, "%u");
	pv(rg, rg_freedi, "%u");
	gfs_inum_print(&rg->rg_freedi_list);

	pv(rg, rg_usedmeta, "%u");
	pv(rg, rg_freemeta, "%u");

	pa(rg, rg_reserved, 64);
}

/**
 * gfs_quota_in - Read in a quota structures
 * @quota: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_quota_in(struct gfs_quota *quota, char *buf)
{
	struct gfs_quota *str = (struct gfs_quota *)buf;

	CPIN_64(quota, str, qu_limit);
	CPIN_64(quota, str, qu_warn);
	CPIN_64(quota, str, qu_value);

	CPIN_08(quota, str, qu_reserved, 64);
}

/**
 * gfs_quota_out - Write out a quota structure
 * @quota: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_quota_out(struct gfs_quota *quota, char *buf)
{
	struct gfs_quota *str = (struct gfs_quota *)buf;

	CPOUT_64(quota, str, qu_limit);
	CPOUT_64(quota, str, qu_warn);
	CPOUT_64(quota, str, qu_value);

	CPOUT_08(quota, str, qu_reserved, 64);
}

/**
 * gfs_quota_print - Print out a quota structure
 * @quota: the cpu-order buffer
 *
 */

void
gfs_quota_print(struct gfs_quota *quota)
{
	pv(quota, qu_limit, "%"PRIu64);
	pv(quota, qu_warn, "%"PRIu64);
	pv(quota, qu_value, "%"PRId64);

	pa(quota, qu_reserved, 64);
}

/**
 * gfs_dinode_in - Read in a dinode
 * @dinode: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_dinode_in(struct gfs_dinode *dinode, char *buf)
{
	struct gfs_dinode *str = (struct gfs_dinode *)buf;

	gfs_meta_header_in(&dinode->di_header, buf);

	gfs_inum_in(&dinode->di_num, (char *)&str->di_num);

	CPIN_32(dinode, str, di_mode);
	CPIN_32(dinode, str, di_uid);
	CPIN_32(dinode, str, di_gid);
	CPIN_32(dinode, str, di_nlink);
	CPIN_64(dinode, str, di_size);
	CPIN_64(dinode, str, di_blocks);
	CPIN_64(dinode, str, di_atime);
	CPIN_64(dinode, str, di_mtime);
	CPIN_64(dinode, str, di_ctime);
	CPIN_32(dinode, str, di_major);
	CPIN_32(dinode, str, di_minor);

	CPIN_64(dinode, str, di_rgrp);
	CPIN_64(dinode, str, di_goal_rgrp);
	CPIN_32(dinode, str, di_goal_dblk);
	CPIN_32(dinode, str, di_goal_mblk);
	CPIN_32(dinode, str, di_flags);
	CPIN_32(dinode, str, di_payload_format);
	CPIN_16(dinode, str, di_type);
	CPIN_16(dinode, str, di_height);
	CPIN_32(dinode, str, di_incarn);
	CPIN_16(dinode, str, di_pad);

	CPIN_16(dinode, str, di_depth);
	CPIN_32(dinode, str, di_entries);

	gfs_inum_in(&dinode->di_next_unused, (char *)&str->di_next_unused);

	CPIN_64(dinode, str, di_eattr);

	CPIN_08(dinode, str, di_reserved, 56);
}

/**
 * gfs_dinode_out - Write out a dinode
 * @dinode: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_dinode_out(struct gfs_dinode *dinode, char *buf)
{
	struct gfs_dinode *str = (struct gfs_dinode *)buf;

	gfs_meta_header_out(&dinode->di_header, buf);

	gfs_inum_out(&dinode->di_num, (char *)&str->di_num);

	CPOUT_32(dinode, str, di_mode);
	CPOUT_32(dinode, str, di_uid);
	CPOUT_32(dinode, str, di_gid);
	CPOUT_32(dinode, str, di_nlink);
	CPOUT_64(dinode, str, di_size);
	CPOUT_64(dinode, str, di_blocks);
	CPOUT_64(dinode, str, di_atime);
	CPOUT_64(dinode, str, di_mtime);
	CPOUT_64(dinode, str, di_ctime);
	CPOUT_32(dinode, str, di_major);
	CPOUT_32(dinode, str, di_minor);

	CPOUT_64(dinode, str, di_rgrp);
	CPOUT_64(dinode, str, di_goal_rgrp);
	CPOUT_32(dinode, str, di_goal_dblk);
	CPOUT_32(dinode, str, di_goal_mblk);
	CPOUT_32(dinode, str, di_flags);
	CPOUT_32(dinode, str, di_payload_format);
	CPOUT_16(dinode, str, di_type);
	CPOUT_16(dinode, str, di_height);
	CPOUT_32(dinode, str, di_incarn);
	CPOUT_16(dinode, str, di_pad);

	CPOUT_16(dinode, str, di_depth);
	CPOUT_32(dinode, str, di_entries);

	gfs_inum_out(&dinode->di_next_unused, (char *)&str->di_next_unused);

	CPOUT_64(dinode, str, di_eattr);

	CPOUT_08(dinode, str, di_reserved, 56);
}

/**
 * gfs_dinode_print - Print out a dinode
 * @di: the cpu-order buffer
 *
 */

void
gfs_dinode_print(struct gfs_dinode *di)
{
	gfs_meta_header_print(&di->di_header);

	gfs_inum_print(&di->di_num);

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

	pv(di, di_rgrp, "%"PRIu64);
	pv(di, di_goal_rgrp, "%"PRIu64);
	pv(di, di_goal_dblk, "%u");
	pv(di, di_goal_mblk, "%u");
	pv(di, di_flags, "0x%.8X");
	pv(di, di_payload_format, "%u");
	pv(di, di_type, "%u");
	pv(di, di_height, "%u");
	pv(di, di_incarn, "%u");
	pv(di, di_pad, "%u");

	pv(di, di_depth, "%u");
	pv(di, di_entries, "%u");

	gfs_inum_print(&di->di_next_unused);

	pv(di, di_eattr, "%"PRIu64);

	pa(di, di_reserved, 56);
}

/**
 * gfs_indirect_in - copy in the header of an indirect block
 * @indirect: the in memory copy
 * @buf: the buffer copy
 *
 */

void
gfs_indirect_in(struct gfs_indirect *indirect, char *buf)
{
	struct gfs_indirect *str = (struct gfs_indirect *)buf;

	gfs_meta_header_in(&indirect->in_header, buf);

	CPIN_08(indirect, str, in_reserved, 64);
}

/**
 * gfs_indirect_out - copy out the header of an indirect block
 * @indirect: the in memory copy
 * @buf: the buffer copy
 *
 */

void
gfs_indirect_out(struct gfs_indirect *indirect, char *buf)
{
	struct gfs_indirect *str = (struct gfs_indirect *)buf;

	gfs_meta_header_out(&indirect->in_header, buf);

	CPOUT_08(indirect, str, in_reserved, 64);
}

/**
 * gfs_indirect_print - Print out a indirect block header
 * @indirect: the cpu-order buffer
 *
 */

void
gfs_indirect_print(struct gfs_indirect *indirect)
{
	gfs_meta_header_print(&indirect->in_header);

	pa(indirect, in_reserved, 64);
}

/**
 * gfs_dirent_in - Read in a directory entry
 * @dirent: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_dirent_in(struct gfs_dirent *dirent, char *buf)
{
	struct gfs_dirent *str = (struct gfs_dirent *)buf;

	gfs_inum_in(&dirent->de_inum, (char *)&str->de_inum);
	CPIN_32(dirent, str, de_hash);
	CPIN_16(dirent, str, de_rec_len);
	CPIN_16(dirent, str, de_name_len);
	CPIN_16(dirent, str, de_type);

	CPIN_08(dirent, str, de_reserved, 14);
}

/**
 * gfs_dirent_out - Write out a directory entry
 * @dirent: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_dirent_out(struct gfs_dirent *dirent, char *buf)
{
	struct gfs_dirent *str = (struct gfs_dirent *)buf;

	gfs_inum_out(&dirent->de_inum, (char *)&str->de_inum);
	CPOUT_32(dirent, str, de_hash);
	CPOUT_16(dirent, str, de_rec_len);
	CPOUT_16(dirent, str, de_name_len);
	CPOUT_16(dirent, str, de_type);

	CPOUT_08(dirent, str, de_reserved, 14);
}

/**
 * gfs_dirent_print - Print out a directory entry
 * @de: the cpu-order buffer
 * @name: the filename
 *
 */

void
gfs_dirent_print(struct gfs_dirent *de, char *name)
{
	char buf[GFS_FNAMESIZE + 1];

	gfs_inum_print(&de->de_inum);
	pv(de, de_hash, "0x%.8X");
	pv(de, de_rec_len, "%u");
	pv(de, de_name_len, "%u");
	pv(de, de_type, "%u");

	pa(de, de_reserved, 14);

	memset(buf, 0, GFS_FNAMESIZE + 1);
	memcpy(buf, name, de->de_name_len);
	printk("  name = %s\n", buf);
}

/**
 * gfs_leaf_in - Read in a directory leaf header
 * @leaf: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_leaf_in(struct gfs_leaf *leaf, char *buf)
{
	struct gfs_leaf *str = (struct gfs_leaf *)buf;

	gfs_meta_header_in(&leaf->lf_header, buf);

	CPIN_16(leaf, str, lf_depth);
	CPIN_16(leaf, str, lf_entries);
	CPIN_32(leaf, str, lf_dirent_format);
	CPIN_64(leaf, str, lf_next);

	CPIN_08(leaf, str, lf_reserved, 64);
}

/**
 * gfs_leaf_out - Write out a directory leaf header
 * @leaf: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_leaf_out(struct gfs_leaf *leaf, char *buf)
{
	struct gfs_leaf *str = (struct gfs_leaf *)buf;

	gfs_meta_header_out(&leaf->lf_header, buf);

	CPOUT_16(leaf, str, lf_depth);
	CPOUT_16(leaf, str, lf_entries);
	CPOUT_32(leaf, str, lf_dirent_format);
	CPOUT_64(leaf, str, lf_next);

	CPOUT_08(leaf, str, lf_reserved, 64);
}

/**
 * gfs_leaf_print - Print out a directory leaf header
 * @lf: the cpu-order buffer
 *
 */

void
gfs_leaf_print(struct gfs_leaf *lf)
{
	gfs_meta_header_print(&lf->lf_header);

	pv(lf, lf_depth, "%u");
	pv(lf, lf_entries, "%u");
	pv(lf, lf_dirent_format, "%u");
	pv(lf, lf_next, "%"PRIu64);

	pa(lf, lf_reserved, 64);
}

/**
 * gfs_log_header_in - Read in a log header
 * @head: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_log_header_in(struct gfs_log_header *head, char *buf)
{
	struct gfs_log_header *str = (struct gfs_log_header *)buf;

	gfs_meta_header_in(&head->lh_header, buf);

	CPIN_32(head, str, lh_flags);
	CPIN_32(head, str, lh_pad);

	CPIN_64(head, str, lh_first);
	CPIN_64(head, str, lh_sequence);

	CPIN_64(head, str, lh_tail);
	CPIN_64(head, str, lh_last_dump);

	CPIN_08(head, str, lh_reserved, 64);
}

/**
 * gfs_log_header_out - Write out a log header
 * @head: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_log_header_out(struct gfs_log_header *head, char *buf)
{
	struct gfs_log_header *str = (struct gfs_log_header *)buf;

	gfs_meta_header_out(&head->lh_header, buf);

	CPOUT_32(head, str, lh_flags);
	CPOUT_32(head, str, lh_pad);

	CPOUT_64(head, str, lh_first);
	CPOUT_64(head, str, lh_sequence);

	CPOUT_64(head, str, lh_tail);
	CPOUT_64(head, str, lh_last_dump);

	CPOUT_08(head, str, lh_reserved, 64);
}

/**
 * gfs_log_header_print - Print out a log header
 * @head: the cpu-order buffer
 *
 */

void
gfs_log_header_print(struct gfs_log_header *lh)
{
	gfs_meta_header_print(&lh->lh_header);

	pv(lh, lh_flags, "0x%.8X");
	pv(lh, lh_pad, "%u");

	pv(lh, lh_first, "%"PRIu64);
	pv(lh, lh_sequence, "%"PRIu64);

	pv(lh, lh_tail, "%"PRIu64);
	pv(lh, lh_last_dump, "%"PRIu64);

	pa(lh, lh_reserved, 64);
}

/**
 * gfs_desc_in - Read in a log descriptor
 * @desc: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_desc_in(struct gfs_log_descriptor *desc, char *buf)
{
	struct gfs_log_descriptor *str = (struct gfs_log_descriptor *)buf;

	gfs_meta_header_in(&desc->ld_header, buf);

	CPIN_32(desc, str, ld_type);
	CPIN_32(desc, str, ld_length);
	CPIN_32(desc, str, ld_data1);
	CPIN_32(desc, str, ld_data2);

	CPIN_08(desc, str, ld_reserved, 64);
}

/**
 * gfs_desc_out - Write out a log descriptor
 * @desc: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_desc_out(struct gfs_log_descriptor *desc, char *buf)
{
	struct gfs_log_descriptor *str = (struct gfs_log_descriptor *)buf;

	gfs_meta_header_out(&desc->ld_header, buf);

	CPOUT_32(desc, str, ld_type);
	CPOUT_32(desc, str, ld_length);
	CPOUT_32(desc, str, ld_data1);
	CPOUT_32(desc, str, ld_data2);

	CPOUT_08(desc, str, ld_reserved, 64);
}

/**
 * gfs_desc_print - Print out a log descriptor
 * @ld: the cpu-order buffer
 *
 */

void
gfs_desc_print(struct gfs_log_descriptor *ld)
{
	gfs_meta_header_print(&ld->ld_header);

	pv(ld, ld_type, "%u");
	pv(ld, ld_length, "%u");
	pv(ld, ld_data1, "%u");
	pv(ld, ld_data2, "%u");

	pa(ld, ld_reserved, 64);
}

/**
 * gfs_block_tag_in - Read in a block tag
 * @tag: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_block_tag_in(struct gfs_block_tag *tag, char *buf)
{
	struct gfs_block_tag *str = (struct gfs_block_tag *)buf;

	CPIN_64(tag, str, bt_blkno);
	CPIN_32(tag, str, bt_flags);
	CPIN_32(tag, str, bt_pad);
}

/**
 * gfs_block_tag_out - Write out a block tag
 * @tag: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_block_tag_out(struct gfs_block_tag *tag, char *buf)
{
	struct gfs_block_tag *str = (struct gfs_block_tag *)buf;

	CPOUT_64(tag, str, bt_blkno);
	CPOUT_32(tag, str, bt_flags);
	CPOUT_32(tag, str, bt_pad);
}

/**
 * gfs_block_tag_print - Print out a block tag
 * @tag: the cpu-order buffer
 *
 */

void
gfs_block_tag_print(struct gfs_block_tag *tag)
{
	pv(tag, bt_blkno, "%"PRIu64);
	pv(tag, bt_flags, "%u");
	pv(tag, bt_pad, "%u");
}

/**
 * gfs_quota_tag_in - Read in a quota tag
 * @tag: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_quota_tag_in(struct gfs_quota_tag *tag, char *buf)
{
	struct gfs_quota_tag *str = (struct gfs_quota_tag *)buf;

	CPIN_64(tag, str, qt_change);
	CPIN_32(tag, str, qt_flags);
	CPIN_32(tag, str, qt_id);
}

/**
 * gfs_quota_tag_out - Write out a quota tag
 * @tag: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_quota_tag_out(struct gfs_quota_tag *tag, char *buf)
{
	struct gfs_quota_tag *str = (struct gfs_quota_tag *)buf;

	CPOUT_64(tag, str, qt_change);
	CPOUT_32(tag, str, qt_flags);
	CPOUT_32(tag, str, qt_id);
}

/**
 * gfs_quota_tag_print - Print out a quota tag
 * @tag: the cpu-order buffer
 *
 */

void
gfs_quota_tag_print(struct gfs_quota_tag *tag)
{
	pv(tag, qt_change, "%"PRId64);
	pv(tag, qt_flags, "0x%.8X");
	pv(tag, qt_id, "%u");
}

/**
 * gfs_ea_header_in - Read in a Extended Attribute header
 * @tag: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_ea_header_in(struct gfs_ea_header *ea, char *buf)
{
	struct gfs_ea_header *str = (struct gfs_ea_header *)buf;

	CPIN_32(ea, str, ea_rec_len);
	CPIN_32(ea, str, ea_data_len);
	ea->ea_name_len = str->ea_name_len;
	ea->ea_type = str->ea_type;
	ea->ea_flags = str->ea_flags;
	ea->ea_num_ptrs = str->ea_num_ptrs;
	CPIN_32(ea, str, ea_pad);
}

/**
 * gfs_ea_header_out - Write out a Extended Attribute header
 * @ea: the cpu-order structure
 * @buf: the disk-order buffer
 *
 */

void
gfs_ea_header_out(struct gfs_ea_header *ea, char *buf)
{
	struct gfs_ea_header *str = (struct gfs_ea_header *)buf;

	CPOUT_32(ea, str, ea_rec_len);
	CPOUT_32(ea, str, ea_data_len);
	str->ea_name_len = ea->ea_name_len;
	str->ea_type = ea->ea_type;
	str->ea_flags = ea->ea_flags;
	str->ea_num_ptrs = ea->ea_num_ptrs;
	CPOUT_32(ea, str, ea_pad);
}

/**
 * gfs_ea_header_printt - Print out a Extended Attribute header
 * @ea: the cpu-order buffer
 *
 */

void
gfs_ea_header_print(struct gfs_ea_header *ea, char *name)
{
	char buf[GFS_EA_MAX_NAME_LEN + 1];

	pv(ea, ea_rec_len, "%u");
	pv(ea, ea_data_len, "%u");
	pv(ea, ea_name_len, "%u");
	pv(ea, ea_type, "%u");
	pv(ea, ea_flags, "%u");
	pv(ea, ea_num_ptrs, "%u");
	pv(ea, ea_pad, "%u");

	memset(buf, 0, GFS_EA_MAX_NAME_LEN + 1);
	memcpy(buf, name, ea->ea_name_len);
	printk("  name = %s\n", buf);
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
 * gfs_dir_hash - hash an array of data
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

uint32_t
gfs_dir_hash(const char *data, int len)
{
	uint32_t hash = 0xFFFFFFFF;

	for (; len--; data++)
		hash = crc_32_tab[(hash ^ *data) & 0xFF] ^ (hash >> 8);

	hash = ~hash;

	return hash;
}

#endif  /* WANT_GFS_CONVERSION_FUNCTIONS */

