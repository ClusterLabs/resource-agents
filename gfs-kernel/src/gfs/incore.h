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

/*
 *  In-core (memory/RAM) structures.
 *  These do not appear on-disk.  See gfs_ondisk.h for on-disk structures.
 */

#ifndef __INCORE_DOT_H__
#define __INCORE_DOT_H__

/*  flags used in function call parameters  */

#define DIO_NEW           (0x00000001)  /* Newly allocated metadata */
#define DIO_FORCE         (0x00000002)  /* Force read of block from disk */
#define DIO_CLEAN         (0x00000004)  /* Don't write to disk */
#define DIO_DIRTY         (0x00000008)  /* Data changed, must write to disk */
#define DIO_START         (0x00000010)  /* Start disk read or write */
#define DIO_WAIT          (0x00000020)  /* Wait for disk r/w to complete */

#define DIO_METADATA      (0x00000040)  /* Process glock's protected metadata */
#define DIO_DATA          (0x00000080)  /* Process glock's protected filedata */
#define DIO_INVISIBLE     (0x00000100)  /* Don't monkey with glock's dirty bit */
#define DIO_CHECK         (0x00000200)  /* Make sure all metadata has been synced */
#define DIO_ALL           (0x00000400)  /* Flush all AIL transactions to disk */

/*  Structure prototypes  */

struct gfs_log_operations;
struct gfs_log_element;
struct gfs_meta_header_cache;
struct gfs_depend;
struct gfs_bitmap;
struct gfs_rgrpd;
struct gfs_bufdata;
struct gfs_glock_operations;
struct gfs_holder;
struct gfs_glock;
struct gfs_alloc;
struct gfs_inode;
struct gfs_file;
struct gfs_unlinked;
struct gfs_quota_le;
struct gfs_quota_data;
struct gfs_log_buf;
struct gfs_trans;
struct gfs_gl_hash_bucket;
struct gfs_sbd;

typedef void (*gfs_glop_bh_t) (struct gfs_glock * gl, unsigned int ret);

/*
 *  Structure of operations that are associated with each
 *  type of element in the log.
 */
struct gfs_log_operations {
	/*  Operations specific to a given log element  */

	void (*lo_add) (struct gfs_sbd * sdp, struct gfs_log_element * le);
	void (*lo_trans_end) (struct gfs_sbd * sdp,
			      struct gfs_log_element * le);
	void (*lo_print) (struct gfs_sbd * sdp, struct gfs_log_element * le,
			  unsigned int where);
	struct gfs_trans *(*lo_overlap_trans) (struct gfs_sbd * sdp,
					       struct gfs_log_element * le);
	void (*lo_incore_commit) (struct gfs_sbd * sdp, struct gfs_trans * tr,
				  struct gfs_log_element * le);
	void (*lo_add_to_ail) (struct gfs_sbd * sdp,
			       struct gfs_log_element * le);
	void (*lo_clean_dump) (struct gfs_sbd * sdp,
			       struct gfs_log_element * le);

	/*  Operations specific to a class of log elements  */

	void (*lo_trans_size) (struct gfs_sbd * sdp, struct gfs_trans * tr,
			       unsigned int *mblks, unsigned int *eblks,
			       unsigned int *blocks, unsigned int *bmem);
	void (*lo_trans_combine) (struct gfs_sbd * sdp, struct gfs_trans * tr,
				  struct gfs_trans * new_tr);
	void (*lo_build_bhlist) (struct gfs_sbd * sdp, struct gfs_trans * tr);
	void (*lo_dump_size) (struct gfs_sbd * sdp, unsigned int *elements,
			      unsigned int *blocks, unsigned int *bmem);
	void (*lo_build_dump) (struct gfs_sbd * sdp, struct gfs_trans * tr);

	/* Operations that happen at recovery time */

	void (*lo_before_scan) (struct gfs_sbd * sdp, unsigned int jid,
				struct gfs_log_header * head,
				unsigned int pass);
	int (*lo_scan_elements) (struct gfs_sbd * sdp,
				 struct gfs_jindex * jdesc,
				 struct gfs_glock * gl, uint64_t start,
				 struct gfs_log_descriptor * desc,
				 unsigned int pass);
	void (*lo_after_scan) (struct gfs_sbd * sdp, unsigned int jid,
			       unsigned int pass);

	/* Type of element (glock/buf/unlinked/quota) */
	char *lo_name;
};

/*
 *  Structure that gets added to struct gfs_trans->tr_elements.  They
 *  make up the "stuff" in each transaction.
 */
struct gfs_log_element {
	struct gfs_log_operations *le_ops; /* Vector of functions */

	struct gfs_trans *le_trans;     /* We're part of this transaction */
	struct list_head le_list;       /* Link to transaction's element list */
};

/*
 * Meta-header cache structure.
 * One for each metadata block that we've read from disk, and are still using.
 * In-core superblock structure hosts the actual cache.
 * Also, each resource group keeps a list of cached blocks within its scope.
 */
struct gfs_meta_header_cache {
	/* Links to various lists */
	struct list_head mc_list_hash;   /* Superblock's hashed list */
	struct list_head mc_list_single; /* Superblock's single list */
	struct list_head mc_list_rgd;    /* Resource group's list */

	uint64_t mc_block;               /* Block # (in-place address) */
	struct gfs_meta_header mc_mh;    /* Payload: the block's meta-header */
};

/*
 * Dependency cache structure.
 * In-core superblock structure hosts the actual cache.
 * Also, each resource group keeps a list of dependency blocks within its scope.
 */
struct gfs_depend {
	/* Links to various lists */
	struct list_head gd_list_hash;  /* Superblock's hashed list */
	struct list_head gd_list_rgd;   /* Resource group's list */

	struct gfs_rgrpd *gd_rgd;       /* Resource group descriptor */
	uint64_t gd_formal_ino;         /* Inode ID */
	unsigned long gd_time;          /* Time (jiffies) when put on list */
};

/*
 *  Block allocation bitmap descriptor structure.
 *  One of these for each FS block that contains bitmap data
 *    (i.e. the resource group header blocks and their following bitmap blocks).
 *  Each allocatable FS data block is represented by 2 bits (4 alloc states).
 */
struct gfs_bitmap {
	uint32_t bi_offset;  /* Byte offset of bitmap within this bit block
	                        (non-zero only for an rgrp header block) */
	uint32_t bi_start;   /* Data block (rgrp scope, 32-bit) represented
	                        by the first bit-pair in this bit block */
	uint32_t bi_len;     /* The number of bitmap bytes in this bit block */
};

/*
 *  Resource Group (Rgrp) descriptor structure.
 *  There is one of these for each resource (block) group in the FS.
 *  The filesystem is divided into a number of resource groups to allow
 *    simultaneous block alloc operations by a number of nodes.
 */
struct gfs_rgrpd {
	/* Links to superblock lists */
	struct list_head rd_list;       /* On-disk-order list of all rgrps */
	struct list_head rd_list_mru;   /* Most Recently Used list of all rgs */
	struct list_head rd_recent;     /* recently used rgrps */

	struct gfs_glock *rd_gl;        /* Glock for this rgrp */

	struct gfs_rindex rd_ri;        /* Resource Index (on-disk) structure */
	struct gfs_rgrp rd_rg;          /* Resource Group (on-disk) structure */
	uint64_t rd_rg_vn;              /* Version #: if != glock's gl_vn,
	                                   we need to read rgrp fm disk */

	/* Block alloc bitmap cache */
	struct gfs_bitmap *rd_bits;     /* Array of block bitmap descriptors */
	struct buffer_head **rd_bh;     /* Array of ptrs to block bitmap bh's */

	/* Block allocation strategy, rgrp scope. Start at these blocks when
	   searching for next data/meta block to alloc */
	uint32_t rd_last_alloc_data;    /* Most recent data block allocated */
	uint32_t rd_last_alloc_meta;    /* Most recent meta block allocated */

	struct list_head rd_mhc;        /* Cached meta-headers for this rgrp */
	struct list_head rd_depend;     /* Dependency elements */

	struct gfs_sbd *rd_sbd;		/* FS incore superblock (fs instance) */
};

/*
 *  Per-buffer data
 *  One of these is attached as GFS private data to each FS block's buffer_head.
 *  These also link into the Active Items Lists (AIL) (buffers flushed to
 *    on-disk log, but not yet flushed to on-disk in-place locations) attached
 *    to 1) the latest transaction to modify and log (on-disk) the buffer,
 *    and 2) the glock that protects the buffer's contents.
 *  Note that multiple transactions can modify the buffer since its most
 *    recent write to disk (in-place location).  Each transaction must log
 *    the modified buffer to the on-disk journal (e.g. 3 transactions
 *    will cause 3 different copies of the buffer to be logged on-disk).
 *    The buffer is attached to only the most recent transaction's AIL
 *    list for a couple of reasons.  One is that only the most up-to-date
 *    buffer content needs to be written to the in-place block on-disk.  The
 *    other is that since there is a more recent copy of the block in
 *    the log, we don't need to keep the older copies in the log.  We can
 *    remove them from the AIL and let the log space be reused for new
 *    transactions.
 *  If a transaction follows another transaction before the first transaction's
 *    log completes, the first transaction's results are copied to a "frozen"
 *    image of the buffer, so it can be logged properly, while the second
 *    transaction is modifying the "real" buffer.
 */
struct gfs_bufdata {
	struct buffer_head *bd_bh;  /* We belong to this Linux buffer_head */
	struct gfs_glock *bd_gl;    /* This glock protects buffer's payload */

	struct gfs_log_element bd_new_le;
	struct gfs_log_element bd_incore_le;

	char *bd_frozen;            /* "Frozen" copy of buffer's data */
	struct semaphore bd_lock;   /* Protects access to this structure */

	/* "Pin" means keep buffer in RAM, don't write to disk (yet) */
	unsigned int bd_pinned;	         /* Recursive pin count */

	/* Links to Active Items Lists */
	struct list_head bd_ail_tr_list; /* This buf's most recent trans' AIL */
	struct list_head bd_ail_gl_list; /* This buf's glock's AIL */
};

/*
 *  Glock operations
 *  One set of operations for each glock, the set selected by type of glock.
 *  These functions get called at various points in a glock's lifetime.
 *  "xmote" = promote (lock) a glock at inter-node level.
 *  "th" = top half, "bh" = bottom half
 */
struct gfs_glock_operations {

	/* Before acquiring a lock at inter-node level */
	void (*go_xmote_th) (struct gfs_glock * gl, unsigned int state,
			     int flags);

	/* After acquiring a lock at inter-node level */
	void (*go_xmote_bh) (struct gfs_glock * gl);

	/* Before releasing a lock at inter-node level, calls go_sync  */
	void (*go_drop_th) (struct gfs_glock * gl);

	/* After releasing a lock at inter-node level, calls go_inval  */
	void (*go_drop_bh) (struct gfs_glock * gl);

	/* Sync dirty data to disk before releasing an inter-node lock
	   (another node needs to read the updated data from disk) */
	void (*go_sync) (struct gfs_glock * gl, int flags);

	/* Invalidate locally cached data just after releasing an inter-node lock
	   (another node may change the on-disk data, so it's no good to us) */
	void (*go_inval) (struct gfs_glock * gl, int flags);

	/* Lock-type-specific check to see if it's okay to unlock a glock */
	int (*go_demote_ok) (struct gfs_glock * gl);

	/* After locking at local process level */
	int (*go_lock) (struct gfs_glock * gl, int flags);

	/* Before unlocking at local process level */
	void (*go_unlock) (struct gfs_glock * gl, int flags);

	/* After receiving a callback: another node needs the lock */
	void (*go_callback) (struct gfs_glock * gl, unsigned int state);

        /* Called when the glock layer marks a lock as being not greedy
	   anymore */
	void (*go_greedy) (struct gfs_glock * gl);

	/* Lock type: locks with same lock # (usually an FS block #),
	   but different types, are different locks */
	int go_type;    /* glock type */
};

/*
 *  Glock holder structure
 *  These coordinate the use, within this node, of an acquired inter-node lock.
 *  One for each holder of a glock.  A glock may be shared within a node by
 *    several processes, or even by several recursive requests from the same
 *    process.  Each is a separate "holder".  To be shared locally, the glock
 *    must be in "SHARED" or "DEFERRED" state at inter-node level, which means
 *    that processes on other nodes might also read the protected entity.
 *  When a process needs to manipulate a lock, it requests it via one of
 *    these holder structures.  If the request cannot be satisfied immediately,
 *    the holder structure gets queued on one of these glock lists:
 *    1) waiters1, for gaining exclusive (local) access to the glock structure.
 *    2) waiters2, for unlocking (demoting) a lock or waiting for a lock
 *       to be unlocked.
 *    3) waiters3, for locking (promoting) a lock.  This may require
 *       changing lock state at inter-node level.
 *  When holding a lock, gfs_holder struct stays on glock's holder list.
 *  See gfs-kernel/src/harness/lm_interface.h for gh_state (LM_ST_...)
 *    and gh_flags (LM_FLAG...) fields.
 *  Also see glock.h for gh_flags field (GL_...) flags.
 */

/*  Action requests  */
#define HIF_MUTEX       (0)  /* Exclusive (local) access to glock struct */
#define HIF_PROMOTE     (1)  /* Change lock to more restrictive state */
#define HIF_DEMOTE      (2)  /* Change lock to less restrictive state */
#define HIF_GREEDY      (3)  /* Wait for the glock to be unlocked */

/*  States  */
#define HIF_ALLOCED     (4)  /* Holder structure is or was in use */
#define HIF_DEALLOC     (5)  /* Holder structure no longer in use */
#define HIF_HOLDER      (6)  /* We have been granted a hold on the lock */
#define HIF_FIRST       (7)  /* We are first on glock's holder list */
#define HIF_RECURSE     (8)  /* Recursive locks on same glock by same process */

struct gfs_holder {
	struct list_head gh_list;      /* Link to one of glock's holder lists */

	struct gfs_glock *gh_gl;       /* Glock that we're holding */
	struct task_struct *gh_owner;  /* Linux process that is the holder */

	/* request to change lock state */
	unsigned int gh_state;         /* LM_ST_... requested lock state */
	int gh_flags;                  /* GL_... or LM_FLAG_... req modifiers */

	int gh_error;                  /* GLR_... CANCELLED or TRYFAILED or -errno */
	unsigned long gh_iflags;       /* HIF_... see above */
	struct completion gh_wait;     /* Wait for completion of ... */
};

/*
 *  Glock Structure
 *  One for each inter-node lock held by this node.
 *  A glock is a local representation/abstraction of an inter-node lock.
 *    Inter-node locks are managed by a "lock module" which plugs in to the
 *    lock harness / glock interface (see gfs-kernel/harness).  Different
 *    lock modules support different lock protocols (e.g. GULM, GDLM, no_lock).
 *  A glock may have one or more holders within a node.  See gfs_holder above.
 *  Glocks are managed within a hash table hosted by the in-core superblock.
 *  After all holders have released a glock, it will stay in the hash table
 *    cache for a time (depending on lock type), during which the inter-node
 *    lock will not be released unless another node needs the lock.  This
 *    provides better performance in case this node needs the glock again soon.
 *  Each glock has an associated vector of lock-type-specific "glops" functions
 *    which are called at important times during the life of a glock, and
 *    which define the type of lock (e.g. dinode, rgrp, non-disk, etc).
 *    See gfs_glock_operations above.
 *  A glock, at inter-node scope, is identified by the following dimensions:
 *    1)  lock number (usually a block # for on-disk protected entities,
 *           or a fixed assigned number for non-disk locks, e.g. MOUNT).
 *    2)  lock type (actually, the type of entity protected by the lock).
 *    3)  lock namespace, to support multiple GFS filesystems simultaneously.
 *           Namespace (usually cluster:filesystem) is specified when mounting.
 *           See man page for gfs_mount.
 *  Glocks require support of Lock Value Blocks (LVBs) by the inter-node lock
 *    manager.  LVBs are small (32-byte) chunks of data associated with a given
 *    lock, that can be quickly shared between cluster nodes.  Used for certain
 *    purposes such as sharing an rgroup's block usage statistics without
 *    requiring the overhead of:
 *      -- sync-to-disk by one node, then a
 *      -- read from disk by another node.
 *  
 */

#define GLF_PLUG                (0)  /* Dummy */
#define GLF_LOCK                (1)  /* Exclusive (local) access to glock structure */
#define GLF_STICKY              (2)  /* Don't unlock this lock unless some explicitly asks */
#define GLF_PREFETCH            (3)  /* This lock has be prefetched, demote if not used soon */
#define GLF_SYNC                (4)  /* This lock should be synced */
#define GLF_DIRTY               (5)  /* There is dirty data for this lock */
#define GLF_LVB_INVALID         (6)  /* LVB does not contain valid data */
#define GLF_SKIP_WAITERS2       (7)  /* Make run_queue() ignore gl_waiters2 holders */
#define GLF_GREEDY              (8)  /* This lock is ignoring callbacks for now */

struct gfs_glock {
	struct list_head gl_list;    /* Link to superblock's hash table */
	unsigned long gl_flags;      /* GLF_... see above */
	struct lm_lockname gl_name;  /* Lock number and lock type */
	atomic_t gl_count;           /* Usage count */

	spinlock_t gl_spin;          /* Protects some members of this struct */

	/* Lock state reflects inter-node manager's lock state */
	unsigned int gl_state;       /* LM_ST_... see harness/lm_interface.h */

	/* Lists of gfs_holders */
	struct list_head gl_holders;  /* all current holders of the glock */
	struct list_head gl_waiters1; /* HIF_MUTEX */
	struct list_head gl_waiters2; /* HIF_DEMOTE, HIF_GREEDY */
	struct list_head gl_waiters3; /* HIF_PROMOTE */

	struct gfs_glock_operations *gl_ops; /* function vector, defines type */

	/* State to remember for async lock requests */
	struct gfs_holder *gl_req_gh; /* The holder that generated the request */
	gfs_glop_bh_t gl_req_bh; /* The bottom half to execute */

	lm_lock_t *gl_lock;       /* Lock module's private lock data */
	char *gl_lvb;             /* Lock Value Block */
	atomic_t gl_lvb_count;    /* LVB recursive usage (hold/unhold) count */

	uint64_t gl_vn;           /* Incremented when protected data changes */
	unsigned long gl_stamp;   /* Glock cache retention timer */
	void *gl_object;          /* The protected entity (e.g. a dinode) */

	/* Incore transaction stuff */
	struct gfs_log_element gl_new_le; /* Incomplete transaction */
	struct gfs_log_element gl_incore_le; /* Complete transaction */ 

	struct gfs_gl_hash_bucket *gl_bucket; /* Our bucket in hash table */
	struct list_head gl_reclaim;          /* Link to "reclaim" list */

	struct gfs_sbd *gl_sbd;               /* Superblock (FS instance) */

	struct inode *gl_aspace;              /* The buffers protected by this lock */
	struct list_head gl_ail_bufs;         /* AIL buffers protected by us */
};

/*
 *  In-Place Reservation structure
 *  Coordinates allocation of "in-place" (as opposed to journal) FS blocks,
 *     which contain persistent inode/file/directory data and metadata.
 *     These blocks are the allocatable blocks within resource groups (i.e.
 *     not including rgrp header and block alloc bitmap blocks).
 *  gfs_inplace_reserve() calculates a fulfillment plan for allocating blocks,
 *     based on block statistics in the resource group headers.
 *  Then, gfs_blkalloc() or gfs_metaalloc() walks the block alloc bitmaps
 *     to do the actual allocation.
 */
struct gfs_alloc {
	/* Up to 4 quotas (including an inode's user and group quotas)
	   can track changes in block allocation */

	unsigned int al_qd_num;          /* # of quotas tracking changes */
	struct gfs_quota_data *al_qd[4]; /* Ptrs to quota structures */
	struct gfs_holder al_qd_ghs[4];  /* Holders for quota glocks */

	/* Request, filled in by the caller to gfs_inplace_reserve() */

	uint32_t al_requested_di;     /* Number of dinodes to reserve */
	uint32_t al_requested_meta;   /* Number of metadata blocks to reserve */
	uint32_t al_requested_data;   /* Number of data blocks to reserve */

	/* Fulfillment plan, filled in by gfs_inplace_reserve() */

	char *al_file;                /* Debug info, .c file making request */
	unsigned int al_line;         /* Debug info, line of code making req */
	struct gfs_holder al_ri_gh;   /* Glock holder for resource grp index */
	struct gfs_holder al_rgd_gh;  /* Glock holder for al_rgd rgrp */
	struct gfs_rgrpd *al_rgd;     /* Resource group from which to alloc */
	uint32_t al_reserved_meta;    /* Alloc up to this # meta blocks from al_rgd */
	uint32_t al_reserved_data;    /* Alloc up to this # data blocks from al_rgd */

	/* Actual alloc, filled in by gfs_blkalloc()/gfs_metaalloc(), etc. */

	uint32_t al_alloced_di;       /* # dinode blocks allocated */
	uint32_t al_alloced_meta;     /* # meta blocks allocated */
	uint32_t al_alloced_data;     /* # data blocks allocated */

	/* Dinode allocation crap */

	struct gfs_unlinked *al_ul;   /* Unlinked dinode log entry */
};

/*
 *  Incore inode structure
 */

#define GIF_QD_LOCKED           (0)
#define GIF_PAGED               (1)
#define GIF_SW_PAGED            (2)

struct gfs_inode {
	struct gfs_inum i_num;   /* Formal inode # and block address */

	atomic_t i_count;        /* Usage count */
	unsigned long i_flags;   /* GIF_...  see above */

	uint64_t i_vn;           /* Version #: if different from glock's vn,
	                            we need to read inode from disk */
	struct gfs_dinode i_di;  /* Dinode (on-disk) structure */

	struct gfs_glock *i_gl;  /* This glock protects this inode */
	struct gfs_sbd *i_sbd;   /* Superblock (fs instance structure) */
	struct inode *i_vnode;   /* Linux VFS inode structure */

	struct gfs_holder i_iopen_gh;  /* Glock holder for Inode Open lock */

	/* Block allocation strategy, inode scope */
	struct gfs_alloc *i_alloc; /* In-place block reservation structure */
	uint64_t i_last_rg_alloc;  /* Most recent block alloc was fm this rgrp */

	spinlock_t i_lock;                /* Protects this structure */

	/* Cache of most-recently used buffers in indirect addressing chain */
	struct buffer_head *i_cache[GFS_MAX_META_HEIGHT];

	unsigned int i_greedy; /* The amount of time to be greedy */
	unsigned long i_last_pfault; /* The time of the last page fault */
};

/*
 *  GFS per-fd structure
 */

#define GFF_DID_DIRECT_ALLOC    (0)

struct gfs_file {
	unsigned long f_flags; /* GFF_...  see above */

	struct semaphore f_fl_lock; /* Lock to protect flock operations */
	struct gfs_holder f_fl_gh; /* Holder for this f_vfile's flock */

	struct gfs_inode *f_inode;        /* Incore GFS inode */
	struct file *f_vfile;             /* Linux file struct */
};

/*
 *  Unlinked inode log entry
 */

#define ULF_NEW_UL              (0)
#define ULF_INCORE_UL           (1)
#define ULF_IC_LIST             (2)
#define ULF_OD_LIST             (3)
#define ULF_LOCK                (4)

struct gfs_unlinked {
	struct list_head ul_list;    /* Link to superblock's sd_unlinked_list */
	unsigned int ul_count;       /* Usage count */

	struct gfs_inum ul_inum;     /* Formal inode #, block addr */
	unsigned long ul_flags;      /* ULF_... */

	struct gfs_log_element ul_new_le;    /* New, not yet committed */
	struct gfs_log_element ul_incore_le; /* Committed to incore log */
	struct gfs_log_element ul_ondisk_le; /* Committed to ondisk log */
};

/*
 *  Quota log element
 *  One for each logged change in a block alloc value affecting a given quota.
 *  Only one of these for a given quota within a given transaction;
 *    multiple changes, within one transaction, for a given quota will be
 *    combined into one log element.
 */
struct gfs_quota_le {
	/* Log element maps us to a particular set of log operations functions,
	   and to a particular transaction */
	struct gfs_log_element ql_le;    /* Generic log element structure */

	struct gfs_quota_data *ql_data;  /* The quota we're changing */
	struct list_head ql_data_list;   /* Link to quota's log element list */

	int64_t ql_change;           /* # of blocks alloc'd (+) or freed (-) */
};

/*
 *  Quota structure
 *  One for each user or group quota.
 *  Summarizes all block allocation activity for a given quota, and supports
 *    recording updates of current block alloc values in GFS' special quota
 *    file, including the journaling of these updates, encompassing
 *    multiple transactions and log dumps.
 */

#define QDF_USER                (0)   /* User (1) vs. group (0) quota */
#define QDF_OD_LIST             (1)   /* Waiting for sync to quota file */
#define QDF_LOCK                (2)   /* Protects access to this structure */

struct gfs_quota_data {
	struct list_head qd_list;     /* Link to superblock's sd_quota_list */
	unsigned int qd_count;        /* Usage count */

	uint32_t qd_id;               /* User or group ID number */
	unsigned long qd_flags;       /* QDF_... */

	/* This list is for non-log-dump transactions */
	struct list_head qd_le_list;  /* List of gfs_quota_le log elements */

	/* Summary of block alloc changes affecting this quota, in various
	   stages of logging & syncing changes to the special quota file */
	int64_t qd_change_new;  /* New, not yet committed to in-core log*/
	int64_t qd_change_ic;   /* Committed to in-core log */
	int64_t qd_change_od;   /* Committed to on-disk log */
	int64_t qd_change_sync; /* Being synced to the in-place quota file */

	struct gfs_quota_le qd_ondisk_ql; /* Log element for log dump */
	uint64_t qd_sync_gen;         /* Sync-to-quota-file generation # */

	/* Glock provides protection for quota, *and* provides
	   lock value block (LVB) communication, between nodes, of current
	   quota values.  Shared lock -> LVB read.  EX lock -> LVB write. */
	struct gfs_glock *qd_gl;      /* glock for this quota */
	struct gfs_quota_lvb qd_qb;   /* LVB (limit/warn/value) */

	unsigned long qd_last_warn;   /* Jiffies of last warning to user */
};

/*
 * Log Buffer descriptor structure
 * One for each FS block buffer recorded in the log
 * lb_bh is a "fake" buffer head that directs Linux block I/O to write the buf
 *   to the on-disk log location, rather than the on-disk in-place location.
 */
struct gfs_log_buf {
	/* Link to one of the transaction structure's lists */
	struct list_head lb_list;      /* Link to tr_free_bufs or tr_list */

	struct buffer_head lb_bh; /* Buffer Head describing the journal block */
	struct buffer_head *lb_unlock; /* Buffer head to unlock after commit */
};

/*
 *  Transaction structure
 *  One for each transaction
 *  This coordinates the logging and flushing of written metadata.
 */

#define TRF_LOG_DUMP            (0x00000001)

struct gfs_trans {

	/* Link to various lists */
	struct list_head tr_list;      /* Superblk's incore trans or AIL list*/

	/* Initial creation stuff */

	char *tr_file;                 /* Debug info: .c file creating trans */
	unsigned int tr_line;          /* Debug info: codeline creating trans */

	/* Reservations for on-disk space in journal */
	unsigned int tr_mblks_asked;   /* # of meta log blocks requested */
	unsigned int tr_eblks_asked;   /* # of extra log blocks requested */
	unsigned int tr_seg_reserved;  /* # of segments actually reserved */

	struct gfs_holder *tr_t_gh;    /* Glock holder for this transaction */

	/* Stuff filled in during creation */

	unsigned int tr_flags;         /* TRF_... */
	struct list_head tr_elements;  /* List of this trans' log elements */

	/* Stuff modified during the commit */

	unsigned int tr_num_free_bufs; /* List of free gfs_log_buf structs */
	struct list_head tr_free_bufs;
	unsigned int tr_num_free_bmem; /* List of free fs-block-size buffers */
	struct list_head tr_free_bmem;

	uint64_t tr_log_head;          /* The current log head */
	uint64_t tr_first_head;	       /* First header block */

	struct list_head tr_bufs;      /* List of buffers going to the log */

	/* Stuff that's part of the Active Items List (AIL) */

	struct list_head tr_ail_bufs;  /* List of buffers on AIL list */

	/* # log elements of various types on tr_elements list */

	unsigned int tr_num_gl;        /* Glocks */
	unsigned int tr_num_buf;       /* Buffers */
	unsigned int tr_num_iul;       /* Unlinked inodes */
	unsigned int tr_num_ida;       /* De-allocated inodes */
	unsigned int tr_num_q;         /* Quotas */
};

/*
 *  One bucket of the glock hash table.
 */
struct gfs_gl_hash_bucket {
	rwlock_t hb_lock;
	struct list_head hb_list;
};

/*
 *  "Super Block" Data Structure
 *  One per mounted filesystem.
 *  This is the big instance structure that ties everything together for
 *    a given mounted filesystem.  Each GFS mount has its own, supporting
 *    mounts of multiple GFS filesystems on each node.
 *  Pointer to this is usually seen as "sdp" throughout code.
 *  This is a very large structure, as structures go, in part because it
 *    contains arrays of hash buckets for various in-core caches.
 */

#define SDF_JOURNAL_LIVE        (0)  /* Journaling is active (journal is writeable)*/

/* Daemon run (1) / stop (0) flags */
#define SDF_SCAND_RUN           (1)  /* Put unused glocks on reclaim queue */
#define SDF_GLOCKD_RUN          (2)  /* Reclaim (dealloc) unused glocks */
#define SDF_RECOVERD_RUN        (3)  /* Recover journal of a crashed node */
#define SDF_LOGD_RUN            (4)  /* Update log tail after AIL flushed */
#define SDF_QUOTAD_RUN          (5)  /* Sync quota changes to file, cleanup */
#define SDF_INODED_RUN          (6)  /* Deallocate unlinked inodes */

/* (Re)mount options from Linux VFS */
#define SDF_NOATIME             (7)  /* Don't change access time */
#define SDF_ROFS                (8)  /* Read-only mode */

/* Journal log dump support */
#define SDF_NEED_LOG_DUMP       (9)  /* Need to rewrite unlink and quota tags */
#define SDF_FOUND_UL_DUMP       (10) /* Recovery found unlinked tags */
#define SDF_FOUND_Q_DUMP        (11) /* Recovery found qutoa tags */
#define SDF_IN_LOG_DUMP         (12) /* Serializes log dumps */

/* Glock cache */
#define GFS_GL_HASH_SHIFT       (13)    /* # hash buckets = 8K */
#define GFS_GL_HASH_SIZE        (1 << GFS_GL_HASH_SHIFT)
#define GFS_GL_HASH_MASK        (GFS_GL_HASH_SIZE - 1)

/* Meta header cache */
#define GFS_MHC_HASH_SHIFT      (10)    /* # hash buckets = 1K */
#define GFS_MHC_HASH_SIZE       (1 << GFS_MHC_HASH_SHIFT)
#define GFS_MHC_HASH_MASK       (GFS_MHC_HASH_SIZE - 1)

/* Dependency cache */
#define GFS_DEPEND_HASH_SHIFT   (10)    /* # hash buckets = 1K */
#define GFS_DEPEND_HASH_SIZE    (1 << GFS_DEPEND_HASH_SHIFT)
#define GFS_DEPEND_HASH_MASK    (GFS_DEPEND_HASH_SIZE - 1)

struct gfs_sbd {
	struct gfs_sb sd_sb;            /* GFS on-disk Super Block image */

	struct super_block *sd_vfs;     /* Linux VFS device independent sb */

	struct gfs_args sd_args;        /* Mount arguments */
	unsigned long sd_flags;         /* SDF_... see above */

	struct gfs_tune sd_tune;	/* Filesystem tuning structure */

	/* Resource group stuff */

	struct gfs_inode *sd_riinode;	/* Resource Index (rindex) inode */
	uint64_t sd_riinode_vn;	        /* Resource Index version # (detects
	                                   whether new rgrps have been added) */

	struct list_head sd_rglist;	/* List of all resource groups,
					   on-disk order */
	struct semaphore sd_rindex_lock;/* Serializes RIndex rereads */
	struct list_head sd_rg_mru_list;/* List of resource groups,
					   most-recently-used (MRU) order */
	spinlock_t sd_rg_mru_lock;      /* Protect mru list */
	struct list_head sd_rg_recent;	/* List of rgrps from which blocks
					   were recently allocated */
	spinlock_t sd_rg_recent_lock;   /* Protect recent list */
	struct gfs_rgrpd *sd_rg_forward;/* Next rgrp from which to attempt
					   a block alloc */
	spinlock_t sd_rg_forward_lock;  /* Protect forward pointer */

	unsigned int sd_rgcount;	/* Total # of resource groups */

	/*  Constants computed on mount  */

	/* "bb" == "basic block" == 512Byte sector */
	uint32_t sd_fsb2bb;             /* # 512B basic blocks in a FS block */
	uint32_t sd_fsb2bb_shift;       /* Shift sector # to the right by 
	                                   this to get FileSystem block addr */
	uint32_t sd_diptrs;     /* Max # of block pointers in a dinode */
	uint32_t sd_inptrs;     /* Max # of block pointers in an indirect blk */
	uint32_t sd_jbsize;     /* Payload size (bytes) of a journaled metadata
	                               block (GFS journals all meta blocks) */
	uint32_t sd_hash_bsize; /* sizeof(exhash hash block) */
	uint32_t sd_hash_bsize_shift;
	uint32_t sd_hash_ptrs;  /* Number of points in a hash block */
	uint32_t sd_max_dirres; /* Max blocks needed to add a directory entry */
	uint32_t sd_max_height;	/* Max height of a file's tree */
	uint64_t sd_heightsize[GFS_MAX_META_HEIGHT];
	uint32_t sd_max_jheight; /* Max height, journaled file's tree */
	uint64_t sd_jheightsize[GFS_MAX_META_HEIGHT];

	/*  Lock Stuff  */

	/* Glock cache (all glocks currently held by this node for this FS) */
	struct gfs_gl_hash_bucket sd_gl_hash[GFS_GL_HASH_SIZE];

	/* Glock reclaim support for scand and glockd */
	struct list_head sd_reclaim_list;   /* List of glocks to reclaim */
	spinlock_t sd_reclaim_lock;
	wait_queue_head_t sd_reclaim_wchan;
	atomic_t sd_reclaim_count;          /* # glocks on reclaim list */

	/* Lock module tells us if we're first-to-mount, 
	   which journal to use, etc. */
	struct lm_lockstruct sd_lockstruct; /* Info provided by lock module */

	/*  Other caches */

	/* Meta-header cache (incore copies of on-disk meta headers) */
	struct list_head sd_mhc[GFS_MHC_HASH_SIZE]; /* hash buckets */
	struct list_head sd_mhc_single;     /* Non-hashed list of all MHCs */
	spinlock_t sd_mhc_lock;
	atomic_t sd_mhc_count;              /* # MHCs in cache */

	/* Dependency cache */
	struct list_head sd_depend[GFS_DEPEND_HASH_SIZE];  /* Hash buckets */
	spinlock_t sd_depend_lock;
	atomic_t sd_depend_count;           /* # dependencies in cache */

	/* LIVE inter-node lock indicates that FS is mounted on at least
	   one node */
	struct gfs_holder sd_live_gh;       /* Glock holder for LIVE lock */

	/* For quiescing the filesystem */
	struct gfs_holder sd_freeze_gh;
	struct semaphore sd_freeze_lock;
	unsigned int sd_freeze_count;

	/*  Inode Stuff  */

	struct gfs_inode *sd_rooti;         /* FS's root inode */

	/* Only 1 node at a time may rename (e.g. mv) directory from
	   one directory to another. */
	struct gfs_glock *sd_rename_gl;     /* Rename glock */

	/*  Daemon stuff  */

	/* Scan for glocks and inodes to toss from memory */
	struct task_struct *sd_scand_process; /* Scand places on reclaim list*/
	unsigned int sd_glockd_num;    /* # of glockd procs to do reclaiming*/

	/* Recover journal of a crashed node */
	struct task_struct *sd_recoverd_process;

	/* Update log tail as AIL gets flushed to in-place on-disk blocks */
	struct task_struct *sd_logd_process;

	/* Sync quota updates to disk, and clean up unused quota structs */
	struct task_struct *sd_quotad_process;

	/* Clean up unused inode structures */
	struct task_struct *sd_inoded_process;

	/* Support for starting/stopping daemons */
	struct semaphore sd_thread_lock;
	struct completion sd_thread_completion;

	/*  Log stuff  */

	/* Transaction lock protects the following from one another:
	   normal write transaction, journal replay (recovery), fs upgrade,
	   fs read-only => read/write and read/write => read-only conversions.
	   Also, acquiring the transaction lock in a state other than shared
	   causes all other machines in the cluster to sync out their dirty
	   data, mark their journal as being clean, and prevent any new FS
	   modifications from occuring (i.e. quiesces the FS). */
	struct gfs_glock *sd_trans_gl;	/* Transaction glock structure */

	struct gfs_inode *sd_jiinode;	/* Journal index inode */
	uint64_t sd_jiinode_vn;         /* Journal index version # (detects
	                                   if new journals have been added) */

	unsigned int sd_journals;	/* Number of journals in the FS */
	struct gfs_jindex *sd_jindex;	/* Array of journal descriptors */
	struct semaphore sd_jindex_lock;
	unsigned long sd_jindex_refresh_time; /* Poll for new journals (secs) */

	struct gfs_jindex sd_jdesc;	 /* This machine's journal descriptor */
	struct gfs_holder sd_journal_gh; /* This machine's journal glock */

	uint64_t sd_sequence;	/* Assigned to xactions in order they commit */
	uint64_t sd_log_head;	/* Block number of next journal write */
	uint64_t sd_log_wrap;

	spinlock_t sd_log_seg_lock;
	unsigned int sd_log_seg_free;	/* # of free segments in the log */
	struct list_head sd_log_seg_list;
	wait_queue_head_t sd_log_seg_wait;

	/* "Active Items List" of transactions that have been flushed to
	   on-disk log, and are waiting for flush to in-place on-disk blocks */
	struct list_head sd_log_ail;	/* "next" is head, "prev" is tail */

	/* Transactions committed incore, but not yet flushed to on-disk log */
	struct list_head sd_log_incore;	/* "Next" is newest, "prev" is oldest */
	unsigned int sd_log_buffers;	/* # of buffers in the incore log */

	struct semaphore sd_log_lock;	/* Lock for access to log values */

	uint64_t sd_log_dump_last;
	uint64_t sd_log_dump_last_wrap;

	/* Unlinked crap */

	struct list_head sd_unlinked_list;
	spinlock_t sd_unlinked_lock;

	atomic_t sd_unlinked_ic_count;
	atomic_t sd_unlinked_od_count;

	/* Quota crap */

	struct list_head sd_quota_list; /* List of all gfs_quota_data structs */
	spinlock_t sd_quota_lock;

	atomic_t sd_quota_count;        /* # quotas on sd_quota_list */
	atomic_t sd_quota_od_count;     /* # quotas waiting for sync to
	                                   special on-disk quota file */

	struct gfs_inode *sd_qinode;    /* Special on-disk quota file */

	uint64_t sd_quota_sync_gen;     /* Generation, incr when sync to file */
	unsigned long sd_quota_sync_time; /* Jiffies, last sync to quota file */

	/* License crap */

	struct gfs_inode *sd_linode;

	/* Recovery stuff */

	struct list_head sd_dirty_j;
	spinlock_t sd_dirty_j_lock;

	unsigned int sd_recovery_replays;
	unsigned int sd_recovery_skips;
	unsigned int sd_recovery_sames;

	/* Counters */

	atomic_t sd_glock_count;
	atomic_t sd_glock_held_count;
	atomic_t sd_inode_count;
	atomic_t sd_bufdata_count;
	atomic_t sd_fh2dentry_misses;
	atomic_t sd_reclaimed;
	atomic_t sd_glock_nq_calls;
	atomic_t sd_glock_dq_calls;
	atomic_t sd_glock_prefetch_calls;
	atomic_t sd_lm_lock_calls;
	atomic_t sd_lm_unlock_calls;
	atomic_t sd_lm_callbacks;
	atomic_t sd_ops_address;
	atomic_t sd_ops_dentry;
	atomic_t sd_ops_export;
	atomic_t sd_ops_file;
	atomic_t sd_ops_inode;
	atomic_t sd_ops_super;
	atomic_t sd_ops_vm;

	char sd_fsname[256];

	/* Debugging crud */

	unsigned long sd_last_readdirplus;
	unsigned long sd_last_unlocked_aop;

	spinlock_t sd_ail_lock;
	struct list_head sd_recovery_bufs;

	struct list_head sd_list;
};

#endif /* __INCORE_DOT_H__ */
