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
 *  These do not appear on-disk.  See gfs2_ondisk.h for on-disk structures.
 */

#ifndef __INCORE_DOT_H__
#define __INCORE_DOT_H__

/* flags used in function call parameters */

#define DIO_FORCE         (0x00000001)  /* Force read of block from disk */
#define DIO_CLEAN         (0x00000002)  /* Don't write to disk */
#define DIO_DIRTY         (0x00000004)  /* Data changed, must write to disk */
#define DIO_START         (0x00000008)  /* Start disk read or write */
#define DIO_WAIT          (0x00000010)  /* Wait for disk r/w to complete */
#define DIO_METADATA      (0x00000020)  /* Process glock's protected metadata */
#define DIO_DATA          (0x00000040)  /* Process glock's protected filedata */
#define DIO_RELEASE       (0x00000080)  /* Releasing glock */
#define DIO_ALL           (0x00000100)  /* Flush all AIL transactions to disk */

/* Structure prototypes */

struct gfs2_log_operations;
struct gfs2_log_element;
struct gfs2_bitmap;
struct gfs2_rgrpd;
struct gfs2_bufdata;
struct gfs2_databuf;
struct gfs2_glock_operations;
struct gfs2_holder;
struct gfs2_glock;
struct gfs2_alloc;
struct gfs2_inode;
struct gfs2_file;
struct gfs2_revoke;
struct gfs2_revoke_replay;
struct gfs2_unlinked;
struct gfs2_quota_data;
struct gfs2_log_buf;
struct gfs2_trans;
struct gfs2_ail;
struct gfs2_jdesc;
struct gfs2_args;
struct gfs2_tune;
struct gfs2_gl_hash_bucket;
struct gfs2_sbd;

typedef void (*gfs2_glop_bh_t) (struct gfs2_glock * gl, unsigned int ret);

/*
 *  Structure of operations that are associated with each
 *  type of element in the log.
 */
struct gfs2_log_operations {
	/*
	 * Operations specific to a given log element (LE).
	 * These are typically executed individually via macros such as LO_ADD.
	 */

	/* Add new LE to transaction */
	void (*lo_add) (struct gfs2_sbd * sdp, struct gfs2_log_element * le);

	/*
	 * Operations specific to a class of log elements.
	 * These are typically executed over a whole transaction by
	 * macros such as LO_BUILD_ENTRY.  Each LE-type-specific operation
	 * for each LE contributes its part to the overall result.
	 */

	void (*lo_incore_commit) (struct gfs2_sbd * sdp, struct gfs2_trans * tr);

	void (*lo_before_commit) (struct gfs2_sbd *sdp);
	void (*lo_after_commit) (struct gfs2_sbd * sdp, struct gfs2_ail * ai);

	void (*lo_before_scan) (struct gfs2_jdesc * jd, struct gfs2_log_header * head,
				int pass);
	int (*lo_scan_elements) (struct gfs2_jdesc * jd, unsigned int start,
				 struct gfs2_log_descriptor * ld, int pass);
	void (*lo_after_scan) (struct gfs2_jdesc * jd, int error, int pass);

	/* 
	 * Type of element (glock/buf)
	 */
	char *lo_name;
};

/*
 *  Structure that gets added to struct gfs2_trans->tr_elements.  They
 *  make up the "stuff" in each transaction.
 */
struct gfs2_log_element {
	struct list_head le_list;
	struct gfs2_log_operations *le_ops;
};

/*
 *  Block allocation bitmap descriptor structure.
 *  One of these for each FS block that contains bitmap data
 *    (i.e. the resource group header blocks and their following bitmap blocks).
 *  Each allocatable FS data block is represented by 2 bits (4 alloc states).
 */
struct gfs2_bitmap {
	struct buffer_head *bi_bh;
	char *bi_clone;

	uint32_t bi_offset;  /* Byte offset of bitmap within this bit block */
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
struct gfs2_rgrpd {
	/* Links to superblock lists */
	struct list_head rd_list;       /* On-disk-order list of all rgrps */
	struct list_head rd_list_mru;   /* Most Recently Used list of all rgs */
	struct list_head rd_recent;     /* recently used rgrps */

	struct gfs2_glock *rd_gl;        /* Glock for this rgrp */

	struct gfs2_rindex rd_ri;        /* Resource Index (on-disk) structure */
	struct gfs2_rgrp rd_rg;          /* Resource Group (on-disk) structure */
	uint64_t rd_rg_vn;               /* Version #: if != glock's gl_vn,
					    we need to read rgrp fm disk */

	/* Block alloc bitmap cache */
	struct gfs2_bitmap *rd_bits;
	unsigned int rd_bh_count;

	struct semaphore rd_mutex;

	uint32_t rd_free_clone;
	struct gfs2_log_element rd_le;

	/* Block allocation strategy, rgrp scope. Start at these blocks when
	   searching for next data/meta block to alloc */
	uint32_t rd_last_alloc_data;    /* Most recent data block allocated */
	uint32_t rd_last_alloc_meta;    /* Most recent meta block allocated */

	struct gfs2_sbd *rd_sbd;	/* FS incore superblock (fs instance) */
};

enum gfs2_state_bits {
	BH_Pinned = BH_PrivateStart,
};

BUFFER_FNS(Pinned, pinned)
TAS_BUFFER_FNS(Pinned, pinned)

/*
 *  Per-buffer data
 *  One of these is attached as GFS2 private data to each FS block's buffer_head.
 *  These keep track of a buffer's progress through the transaction pipeline,
 *    using the "new" embedded log element to attach it to a being-built
 *    transaction, and moving the attachment point to the "incore" LE once
 *    the transaction completes (at which time the buffer becomes a candidate
 *    to be written to the on-disk log).
 *  A buffer may be attached simultaneously to a new and an incore transaction,
 *    but no more than one of each:  Only one new trans may be built at a time
 *    for a given buffer, obviously, since the buffer's contents are protected
 *    by an EXclusive glock when writing.  And, when a transaction is completely
 *    built, GFS2 combines incore transactions that share glocks (see
 *    incore_commit()), i.e. the glock that protects the buffer, so a buffer
 *    never needs to be attached to more than one (combined) incore trans.
 *  Note that multiple transactions can modify the buffer since its most
 *    recent writes to disk.  This principle applies to both in-place and
 *    journal block locations on-disk, allowing this node to keep modifying the
 *    cached data without writing it to disk, unless/until another node needs
 *    to access the data, or the Linux OS tells us to sync to disk.
 *  If a transaction follows another transaction before the first transaction's
 *    log completes (indicated by the in-place buffer head still being pinned
 *    in RAM), GFS2 copies the first transaction's results to a "frozen"
 *    image of the buffer, so the first transaction results (an atomic
 *    snapshot) can be logged properly, while the second transaction is
 *    modifying the "real" buffer.  This frozen copy lives only until the new
 *    transaction is complete, at which point one of two things has occurred:
 *    1).  Buffer was logged successfully; frozen copy's job is done.
 *    2).  Buffer was not yet logged; frozen copy no longer needed, newer
 *         buffer becomes the log candidate.
 *
 *  gfs2_bufdata structs also link into the Active Items Lists (AIL) (buffers
 *    flushed to on-disk log, but not yet flushed to on-disk in-place locations)
 *    attached to:
 *    1).  The latest transaction to modify and log (on-disk) the buffer, and
 *    2).  The glock that protects the buffer's contents.
 *  The buffer is attached to only the most recent transaction's AIL
 *    list for a couple of reasons.  One is that only the most up-to-date
 *    buffer content needs to be written to the in-place block on-disk.  The
 *    other is that since there is a more recent copy of the block in
 *    the log, we don't need to keep the older copies in the log.  We can
 *    remove them from the AIL and let the log space be reused for new
 *    transactions (GFS2 advances the log tail when removing buffers from AIL).
 */
struct gfs2_bufdata {
	struct buffer_head *bd_bh;  /* We belong to this Linux buffer_head */
	struct gfs2_glock *bd_gl;    /* This glock protects buffer's payload */

	struct list_head bd_list_tr;
	struct gfs2_log_element bd_le;

	/* Links to Active Items Lists */
	struct gfs2_ail *bd_ail;
	struct list_head bd_ail_st_list; /* This buf's most recent struct gfs2_ail */
	struct list_head bd_ail_gl_list; /* This buf's glock's AIL */
};

struct gfs2_databuf {
	struct gfs2_log_element db_le;
	struct buffer_head *db_bh;
};

/*
 *  Glock operations
 *  One set of operations for each glock, the set selected by type of glock.
 *  These functions get called at various points in a glock's lifetime.
 *  "xmote" = promote or demote (change lock state) a glock at inter-node scope.
 *  "th" = top half, "bh" = bottom half
 *  Some operations/fields are required (GFS2 assumes they are there):
 *     go_xmote_th
 *     go_drop_th
 *     go_type
 *  Other operations are optional (GFS2 checks for presence before calling).
 */
struct gfs2_glock_operations {

	/* Acquire lock or change lock state at inter-node scope:
	     Does type-specific preparation (if any)
	     Uses gfs2_glock_xmote_th to call lock module. */
	void (*go_xmote_th) (struct gfs2_glock * gl, unsigned int state,
			     int flags);

	/* After acquiring or changing a lock at inter-node scope */
	void (*go_xmote_bh) (struct gfs2_glock * gl);

	/* Release (unlock) a lock at inter-node scope:
	     Does type-specific preparation (if any)
	     Uses gfs2_glock_drop_th to call lock module. */
	void (*go_drop_th) (struct gfs2_glock * gl);

	/* After releasing a lock at inter-node scope */
	void (*go_drop_bh) (struct gfs2_glock * gl);

	/* Sync dirty data to disk (e.g. before demoting an EX inter-node lock)
	   (another node needs to read the updated data from disk) */
	void (*go_sync) (struct gfs2_glock * gl, int flags);

	/* Invalidate local cached data just after releasing an inter-node lock
	   (another node may change the on-disk data, so it's no good to us) */
	void (*go_inval) (struct gfs2_glock * gl, int flags);

	/* Lock-type-specific check to see if it's okay to unlock a glock
	   at inter-node scope (and remove it from our glock cache) */
	int (*go_demote_ok) (struct gfs2_glock * gl);

	/* After getting lock for first holder (within this node) */
	int (*go_lock) (struct gfs2_holder * gh);

	/* After last holder (within this node) gives up lock (glock may
	   remain in glock cache, though) */
	void (*go_unlock) (struct gfs2_holder * gh);

	/* After receiving a callback: another node needs the lock */
	void (*go_callback) (struct gfs2_glock * gl, unsigned int state);

        /* Called when the glock layer marks a lock as being not greedy
	   anymore */
	void (*go_greedy) (struct gfs2_glock * gl);

	/* Lock type: locks with same lock # (often an FS block #),
	   but different types, are different locks */
	int go_type;
};

/*
 *  Glock holder structure
 *  One for each holder of a glock.
 *  These coordinate the use, within this node, of an acquired inter-node glock.
 *  Once a node has acquired a glock, it may be shared within that node by
 *    several processes, or even by several recursive requests from the same
 *    process.  Each is a separate "holder".  Different holders may co-exist
 *    having requested different lock states, as long as the node holds the
 *    glock in a state that is compatible.  A hold requestor may select, via
 *    flags, the rules by which sharing within the node is granted:
 *      LM_FLAG_ANY:  Grant if glock state is any other than UNLOCKED.
 *      GL_EXACT:     Grant only if glock state is exactly the requested state.
 *      GL_LOCAL_EXCL:  Grant only one holder at a time within this node.
 *    With no flags, a hold will be granted to a SHARED request even if the
 *    node holds the glock in EXCLUSIVE mode.  See relaxed_state_ok().
 *  When a process needs to manipulate a lock, it requests it via one of
 *    these holder structures.  If the request cannot be satisfied immediately,
 *    the holder structure gets queued on one of these lists in gfs2_glock:
 *    1) waiters1, for gaining exclusive access to the (local) glock structure.
 *    2) waiters2, for demoting a lock (unlocking a glock, or changing its state
 *       to be less restrictive) or relenquishing "greedy" status.
 *    3) waiters3, for promoting (locking a new glock, or changing a glock state
 *       to be more restrictive).
 *  When holding a lock, gfs2_holder struct stays on glock's holder list.
 *  See gfs2-kernel/src/harness/lm_interface.h for gh_state (LM_ST_...)
 *    and gh_flags (LM_FLAG...) fields.
 *  Also see glock.h for gh_flags field (GL_...) flags.
 */

/* Action requests */
#define HIF_MUTEX       (0)  /* Exclusive (local) access to glock struct */
#define HIF_PROMOTE     (1)  /* Change lock to more restrictive state */
#define HIF_DEMOTE      (2)  /* Change lock to less restrictive state */
#define HIF_GREEDY      (3)  /* Wait for the glock to be unlocked */

/* States */
#define HIF_ALLOCED     (4)  /* Holder structure is or was in use */
#define HIF_DEALLOC     (5)  /* Toss holder struct as soon as queued request
                              *   is satisfied */
#define HIF_HOLDER      (6)  /* We have been granted a hold on the lock */
#define HIF_FIRST       (7)  /* We are first holder to get the lock */
#define HIF_RECURSE     (8)  /* >1 hold requests on same glock by same process*/
#define HIF_ABORTED     (9) /* Aborted before being submitted */

struct gfs2_holder {
	struct list_head gh_list;      /* Link to one of glock's holder lists */

	struct gfs2_glock *gh_gl;       /* Glock that we're holding */
	struct task_struct *gh_owner;  /* Linux process that is the holder */

	/* request to change lock state */
	unsigned int gh_state;         /* LM_ST_... requested lock state */
	int gh_flags;                  /* GL_... or LM_FLAG_... req modifiers */

	int gh_error;                  /* GLR_... CANCELLED/TRYFAILED/-errno */
	unsigned long gh_iflags;       /* HIF_... holder state, see above */
	struct completion gh_wait;     /* Wait for completion of ... */
};

/*
 *  Glock Structure
 *  One for each inter-node lock held by this node.
 *  A glock is a local representation/abstraction of an inter-node lock.
 *    Inter-node locks are managed by a "lock module" (LM) which plugs in to
 *    the lock harness / glock interface (see gfs2-kernel/harness).  Different
 *    lock modules support different lock protocols (e.g. GULM, GDLM, no_lock).
 *  A glock may have one or more holders within a node.  See gfs2_holder above.
 *  Glocks are managed within a hash table hosted by the in-core superblock.
 *  After all holders have released a glock, it will stay in the hash table
 *    cache for a time (depending on lock type), during which the inter-node
 *    lock will not be released unless another node needs the lock (lock
 *    manager requests this via callback to GFS2 through LM on this node).  This
 *    provides better performance in case this node needs the glock again soon.
 *    See comments for meta_go_demote_ok(), glops.c.
 *  Each glock has an associated vector of lock-type-specific "glops" functions
 *    which are called at important times during the life of a glock, and
 *    which define the type of lock (e.g. dinode, rgrp, non-disk, etc).
 *    See gfs2_glock_operations above.
 *  A glock, at inter-node scope, is identified by the following dimensions:
 *    1)  lock number (usually a block # for on-disk protected entities,
 *           or a fixed assigned number for non-disk locks, e.g. MOUNT).
 *    2)  lock type (actually, the type of entity protected by the lock).
 *    3)  lock namespace, to support multiple GFS2 filesystems simultaneously.
 *           Namespace (usually cluster:filesystem) is specified when mounting.
 *           See man page for gfs2_mount.
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
#define GLF_LOCK                (1)  /* Exclusive (local) access to glock
                                      *   structure */
#define GLF_STICKY              (2)  /* Don't release this inter-node lock
                                      *   unless another node explicitly asks */
#define GLF_PREFETCH            (3)  /* This lock has been (speculatively)
                                      *   prefetched, demote if not used soon */
#define GLF_SYNC                (4)  /* Sync lock's protected data as soon as
                                      *   there are no more holders */
#define GLF_DIRTY               (5)  /* There is dirty data for this lock,
                                      *   sync before releasing inter-node */
#define GLF_SKIP_WAITERS2       (6)  /* Make run_queue() ignore gl_waiters2
                                      *   (demote/greedy) holders */
#define GLF_GREEDY              (7)  /* This lock is ignoring callbacks
                                      *   (requests from other nodes) for now */

struct gfs2_glock {
	struct list_head gl_list;    /* Link to hb_list in one of superblock's
	                              * sd_gl_hash glock hash table buckets */
	unsigned long gl_flags;      /* GLF_... see above */
	struct lm_lockname gl_name;  /* Lock number and lock type */
	atomic_t gl_count;           /* Usage count */

	spinlock_t gl_spin;          /* Protects some members of this struct */

	/* Lock state reflects inter-node manager's lock state */
	unsigned int gl_state;       /* LM_ST_... see harness/lm_interface.h */

	/* Lists of gfs2_holders */
	struct list_head gl_holders;  /* all current holders of the glock */
	struct list_head gl_waiters1; /* HIF_MUTEX */
	struct list_head gl_waiters2; /* HIF_DEMOTE, HIF_GREEDY */
	struct list_head gl_waiters3; /* HIF_PROMOTE */

	struct gfs2_glock_operations *gl_ops; /* function vector, defines type */

	/* State to remember for async lock requests */
	struct gfs2_holder *gl_req_gh; /* Holder for request being serviced */
	gfs2_glop_bh_t gl_req_bh;  /* The bottom half to execute */

	lm_lock_t *gl_lock;       /* Lock module's private lock data */
	char *gl_lvb;             /* Lock Value Block */
	atomic_t gl_lvb_count;    /* LVB recursive usage (hold/unhold) count */

	uint64_t gl_vn;           /* Incremented when protected data changes */
	unsigned long gl_stamp;   /* Glock cache retention timer */
	void *gl_object;          /* The protected entity (e.g. a dinode) */

	struct gfs2_gl_hash_bucket *gl_bucket; /* Our bucket in sd_gl_hash */
	struct list_head gl_reclaim;          /* Link to sd_reclaim_list */

	struct gfs2_sbd *gl_sbd;               /* Superblock (FS instance) */

	struct inode *gl_aspace;              /* The buffers protected by this lock */

	struct gfs2_log_element gl_le;
	struct list_head gl_ail_list;         /* AIL buffers protected by us */
	atomic_t gl_ail_count;
};

/*
 *  In-Place Reservation structure
 *  Coordinates allocation of "in-place" (as opposed to journal) FS blocks,
 *     which contain persistent inode/file/directory data and metadata.
 *     These blocks are the allocatable blocks within resource groups (i.e.
 *     not including rgrp header and block alloc bitmap blocks).
 *  gfs2_inplace_reserve() calculates a fulfillment plan for allocating blocks,
 *     based on block statistics in the resource group headers.
 *  Then, gfs2_alloc_*() walks the block alloc bitmaps to do the actual
 *     allocation.
 */
struct gfs2_alloc {
	/* Up to 4 quotas (including an inode's user and group quotas)
	   can track changes in block allocation */

	unsigned int al_qd_num;          /* # of quotas tracking changes */
	struct gfs2_quota_data *al_qd[4]; /* Ptrs to quota structures */
	struct gfs2_holder al_qd_ghs[4];  /* Holders for quota glocks */

	/* Request, filled in by the caller to gfs2_inplace_reserve() */

	uint32_t al_requested;        /* Number of blockss to reserve */

	/* Fulfillment plan, filled in by gfs2_inplace_reserve() */

	char *al_file;                /* Debug info, .c file making request */
	unsigned int al_line;         /* Debug info, line of code making req */
	struct gfs2_holder al_ri_gh;   /* Glock holder for resource grp index */
	struct gfs2_holder al_rgd_gh;  /* Glock holder for al_rgd rgrp */
	struct gfs2_rgrpd *al_rgd;     /* Resource group from which to alloc */

	/* Actual alloc, filled in by gfs2_alloc_*(). */

	uint32_t al_alloced;          /* # blocks allocated */
};

/*
 *  Incore inode structure
 */

#define GIF_MIN_INIT            (0)
#define GIF_QD_LOCKED           (1)
#define GIF_PAGED               (2)
#define GIF_SW_PAGED            (3)

struct gfs2_inode {
	struct gfs2_inum i_num;   /* Formal inode # and block address */

	atomic_t i_count;        /* Usage count */
	unsigned long i_flags;   /* GIF_...  see above */

	uint64_t i_vn;           /* Version #: if different from glock's vn,
	                            we need to read inode from disk */
	struct gfs2_dinode i_di;  /* Dinode (on-disk) structure */

	struct gfs2_glock *i_gl;  /* This glock protects this inode */
	struct gfs2_sbd *i_sbd;   /* Superblock (fs instance structure) */
	struct inode *i_vnode;   /* Linux VFS inode structure */

	struct gfs2_holder i_iopen_gh;  /* Glock holder for Inode Open lock */

	/* Block allocation strategy, inode scope */
	struct gfs2_alloc *i_alloc; /* In-place block reservation structure */
	uint64_t i_last_rg_alloc;  /* Most recent blk alloc was fm this rgrp */

	spinlock_t i_spin;
	struct rw_semaphore i_rw_mutex;

	unsigned int i_greedy; /* The amount of time to be greedy */
	unsigned long i_last_pfault; /* The time of the last page fault */

	/* Cache of most-recently used buffers in indirect addressing chain */
	struct buffer_head *i_cache[GFS2_MAX_META_HEIGHT];
};

/*
 *  GFS2 per-fd structure
 */

#define GFF_DID_DIRECT_ALLOC    (0)

struct gfs2_file {
	unsigned long f_flags; /* GFF_...  see above */

	struct semaphore f_fl_mutex; /* Lock to protect flock operations */
	struct gfs2_holder f_fl_gh; /* Holder for this f_vfile's flock */

	struct gfs2_inode *f_inode;       /* Incore GFS2 inode */
	struct file *f_vfile;             /* Linux file struct */
};

struct gfs2_revoke {
	struct gfs2_log_element rv_le;
	uint64_t rv_blkno;
};

struct gfs2_revoke_replay {
	struct list_head rr_list;
	uint64_t rr_blkno;
	unsigned int rr_where;
};

/*
 *  Unlinked inode log entry incore structure
 */

#define ULF_LOCKED (0)

struct gfs2_unlinked {
	struct list_head ul_list;    /* Link to superblock's sd_unlinked_list */
	unsigned int ul_count;
	struct gfs2_unlinked_tag ul_ut;
	unsigned long ul_flags;  /* ULF_... */
	unsigned int ul_slot;
};

/*
 *  Quota structure
 *  One for each user or group quota.
 *  Summarizes all block allocation activity for a given quota, and supports
 *    recording updates of current block alloc values in GFS2' special quota
 *    file, including the journaling of these updates, encompassing
 *    multiple transactions and log dumps.
 */

#define QDF_USER                (0)   /* User (1) vs. group (0) quota */
#define QDF_CHANGE              (1)
#define QDF_LOCKED              (2)   /* Protects access to this structure */

struct gfs2_quota_data {
	struct list_head qd_list;     /* Link to superblock's sd_quota_list */
	unsigned int qd_count;        /* Usage count */

	uint32_t qd_id;               /* User or group ID number */
	unsigned long qd_flags;       /* QDF_... */

	int64_t qd_change;
	int64_t qd_change_sync;

	unsigned int qd_slot;
	unsigned int qd_slot_count;

	struct buffer_head *qd_bh;
	struct gfs2_quota_change *qd_bh_qc;
	unsigned int qd_bh_count;

	struct gfs2_glock *qd_gl;      /* glock for this quota */
	struct gfs2_quota_lvb qd_qb;   /* LVB (limit/warn/value) */

	uint64_t qd_sync_gen;         /* Sync-to-quota-file generation # */
	unsigned long qd_last_warn;
	unsigned long qd_last_touched;
};

/*
 * Log Buffer descriptor structure.
 */
struct gfs2_log_buf {
	struct list_head lb_list;      /* Link to tr_free_bufs or tr_list */
	struct buffer_head *lb_bh;      /* "Fake" bh; for the log block */
	struct buffer_head *lb_real;
};

/*
 *  Transaction structure
 *  One for each transaction
 *  This coordinates the logging and flushing of written metadata.
 */

struct gfs2_trans {
	char *tr_file;                 /* Debug info: .c file creating trans */
	unsigned int tr_line;          /* Debug info: codeline creating trans */

	unsigned int tr_blocks;
	unsigned int tr_revokes;
	unsigned int tr_reserved;      /* on-disk space in journal. */

	struct gfs2_holder *tr_t_gh;    /* Glock holder for this transaction */

	int tr_touched;

	unsigned int tr_num_buf;
	unsigned int tr_num_buf_new;
	unsigned int tr_num_buf_rm;
	struct list_head tr_list_buf;

	unsigned int tr_num_revoke;
	unsigned int tr_num_revoke_rm;
};

struct gfs2_ail {
	struct list_head ai_list;

	unsigned int ai_first;
	struct list_head ai_ail1_list;
	struct list_head ai_ail2_list;

	uint64_t ai_sync_gen;
};

struct gfs2_jdesc {
	struct list_head jd_list;

	struct gfs2_inode *jd_inode;
	unsigned int jd_jid;
	int jd_dirty;

	unsigned int jd_blocks;
};

#define GFS2_GLOCKD_DEFAULT (1)
#define GFS2_GLOCKD_MAX (32)

#define GFS2_QUOTA_DEFAULT GFS2_QUOTA_OFF
#define GFS2_QUOTA_OFF (0)
#define GFS2_QUOTA_ACCOUNT (1)
#define GFS2_QUOTA_ON (2)

#define GFS2_DATA_DEFAULT GFS2_DATA_ORDERED
#define GFS2_DATA_WRITEBACK (1)
#define GFS2_DATA_ORDERED (2)

struct gfs2_args {
	char ar_lockproto[GFS2_LOCKNAME_LEN]; /* The name of the Lock Protocol */
	char ar_locktable[GFS2_LOCKNAME_LEN]; /* The name of the Lock Table */
	char ar_hostdata[GFS2_LOCKNAME_LEN]; /* The host specific data */

	int ar_spectator; /* Don't get a journal because we're always RO. */

        /*
	 * GFS2 can invoke some flock and disk caching optimizations if it is
	 * not in a cluster, i.e. is a local filesystem.  The chosen lock
	 * module tells GFS2, at mount time, if it supports clustering.
	 * The nolock module is the only one that does not support clustering;
	 * it sets to TRUE the local_fs field in the struct lm_lockops.
	 * GFS2 can either optimize, or ignore the opportunity.
	 * The user controls behavior via the following mount options.
	 */
	int ar_ignore_local_fs; /* Don't optimize even if local_fs is TRUE */
	int ar_localflocks; /* Let the VFS do flock|fcntl locks for us */
	int ar_localcaching; /* Local-style caching (dangerous on multihost) */
	int ar_oopses_ok; /* Allow oopses */

	int ar_debug; /* Oops on errors instead of trying to be graceful */
	int ar_upgrade; /* Upgrade ondisk/multihost format */
	unsigned int ar_num_glockd; /* # of glock cleanup daemons to run
				       (more daemons => faster cleanup) */
	int ar_posix_acl; /* Enable posix acls */
	int ar_quota; /* off/account/on */
	int ar_suiddir; /* suiddir support */
	int ar_data; /* ordered/writeback */
};

struct gfs2_tune {
	spinlock_t gt_spin;

	unsigned int gt_ilimit;
	unsigned int gt_ilimit_tries;
	unsigned int gt_ilimit_min;
	unsigned int gt_demote_secs; /* Cache retention for unheld glock */
	unsigned int gt_incore_log_blocks;
	unsigned int gt_log_flush_secs;
	unsigned int gt_jindex_refresh_secs; /* Check for new journal index */

	/* How often various daemons run (seconds) */
	unsigned int gt_scand_secs; /* Find unused glocks and inodes */
	unsigned int gt_recoverd_secs; /* Recover journal of crashed node */
	unsigned int gt_logd_secs; /* Update log tail as AIL flushes */
	unsigned int gt_quotad_secs; /* Sync changes to quota file, clean*/
	unsigned int gt_inoded_secs; /* Toss unused inodes */

	unsigned int gt_quota_simul_sync; /* Max # quotavals to sync at once */
	unsigned int gt_quota_warn_period; /* Secs between quota warn msgs */
	unsigned int gt_quota_scale_num; /* Numerator */
	unsigned int gt_quota_scale_den; /* Denominator */
	unsigned int gt_quota_cache_secs;
	unsigned int gt_quota_quantum; /* Secs between syncs to quota file */
	unsigned int gt_atime_quantum; /* Min secs between atime updates */
	unsigned int gt_new_files_jdata;
	unsigned int gt_new_files_directio;
	unsigned int gt_max_atomic_write; /* Split large writes into this size*/
	unsigned int gt_max_readahead; /* Max bytes to read-ahead from disk */
	unsigned int gt_lockdump_size;
	unsigned int gt_stall_secs; /* Detects trouble! */
	unsigned int gt_complain_secs;
	unsigned int gt_reclaim_limit; /* Max # glocks in reclaim list */
	unsigned int gt_entries_per_readdir;
	unsigned int gt_prefetch_secs; /* Usage window for prefetched glocks */
	unsigned int gt_greedy_default;
	unsigned int gt_greedy_quantum;
	unsigned int gt_greedy_max;
	unsigned int gt_statfs_quantum;
	unsigned int gt_statfs_slow;
};

/*
 *  One bucket of the filesystem's sd_gl_hash glock hash table.
 *
 *  A gfs2_glock links into a bucket's list via glock's gl_list member.
 *
 */
struct gfs2_gl_hash_bucket {
	rwlock_t hb_lock;              /* Protects list */
	struct list_head hb_list;      /* List of glocks in this bucket */
};

/*
 *  "Super Block" Data Structure
 *  One per mounted filesystem.
 *  This is the big instance structure that ties everything together for
 *    a given mounted filesystem.  Each GFS2 mount has its own, supporting
 *    mounts of multiple GFS2 filesystems on each node.
 *  Pointer to this is usually seen as "sdp" throughout code.
 *  This is a very large structure, as structures go, in part because it
 *    contains arrays of hash buckets for various in-core caches.
 */

#define SDF_JOURNAL_CHECKED     (0)
#define SDF_JOURNAL_LIVE        (1)  /* Journaling is active (journal is writeable)*/
#define SDF_SHUTDOWN            (2)  /* FS abnormaly shutdown */

/* Run (1) / stop (0) flags for various daemons */
#define SDF_SCAND_RUN           (3)  /* Put unused glocks on reclaim queue */
#define SDF_GLOCKD_RUN          (4)  /* Reclaim (dealloc) unused glocks */
#define SDF_RECOVERD_RUN        (5)  /* Recover journal of a crashed node */
#define SDF_LOGD_RUN            (6)  /* Update log tail after AIL flushed */
#define SDF_QUOTAD_RUN          (7)  /* Sync quota changes to file, cleanup */
#define SDF_INODED_RUN          (8)  /* Deallocate unlinked inodes */

/* (Re)mount options from Linux VFS */
#define SDF_NOATIME             (9)  /* Don't change access time */

/* Glock cache */
#define GFS2_GL_HASH_SHIFT       (13)    /* # hash buckets = 8K */
#define GFS2_GL_HASH_SIZE        (1 << GFS2_GL_HASH_SHIFT)
#define GFS2_GL_HASH_MASK        (GFS2_GL_HASH_SIZE - 1)

struct gfs2_sbd {
	struct super_block *sd_vfs;     /* Linux VFS device independent sb */

	unsigned long sd_flags;         /* SDF_... see above */
	struct gfs2_sb sd_sb;            /* GFS2 on-disk Super Block image */

	/* Constants computed on mount */

	/* "bb" == "basic block" == 512Byte sector */
	uint32_t sd_fsb2bb;             /* # 512B basic blocks in a FS block */
	uint32_t sd_fsb2bb_shift;       /* Shift sector # to the right by 
	                                   this to get FileSystem block addr */
	uint32_t sd_diptrs;     /* Max # of block pointers in a dinode */
	uint32_t sd_inptrs;     /* Max # of block pointers in an indirect blk */
	uint32_t sd_jbsize;     /* Payload size (bytes) of a journaled metadata
	                               block (GFS2 journals all meta blocks) */
	uint32_t sd_hash_bsize; /* sizeof(exhash hash block) */
	uint32_t sd_hash_bsize_shift;
	uint32_t sd_hash_ptrs;  /* Number of points in a hash block */
	uint32_t sd_ut_per_block;
	uint32_t sd_qc_per_block;
	uint32_t sd_max_dirres; /* Max blocks needed to add a directory entry */
	uint32_t sd_max_height;	/* Max height of a file's tree */
	uint64_t sd_heightsize[GFS2_MAX_META_HEIGHT];
	uint32_t sd_max_jheight; /* Max height, journaled file's tree */
	uint64_t sd_jheightsize[GFS2_MAX_META_HEIGHT];

	struct gfs2_args sd_args;        /* Mount arguments */
	struct gfs2_tune sd_tune;	/* Filesystem tuning structure */

	/* Lock Stuff */

	/* Lock module tells us if we're first-to-mount, 
	   which journal to use, etc. */
	struct lm_lockstruct sd_lockstruct; /* Info provided by lock module */

	/* Glock cache (all glocks currently held by this node for this FS) */
	struct gfs2_gl_hash_bucket sd_gl_hash[GFS2_GL_HASH_SIZE];

	/* Glock reclaim support for scand and glockd */
	struct list_head sd_reclaim_list;   /* List of glocks to reclaim */
	spinlock_t sd_reclaim_lock;
	wait_queue_head_t sd_reclaim_wq;
	atomic_t sd_reclaim_count;          /* # glocks on reclaim list */

	/* LIVE inter-node lock indicates that FS is mounted on at least
	   one node */
	struct gfs2_holder sd_live_gh;       /* Glock holder for LIVE lock */

	/* Only 1 node at a time may rename (e.g. mv) directory from
	   one directory to another. */
	struct gfs2_glock *sd_rename_gl;     /* Rename glock */

	/* Transaction lock protects the following from one another:
	   normal write transaction, journal replay (recovery), fs upgrade,
	   fs read-only => read/write and read/write => read-only conversions.
	   Also, acquiring the transaction lock in a state other than shared
	   causes all other machines in the cluster to sync out their dirty
	   data, mark their journal as being clean, and prevent any new FS
	   modifications from occuring (i.e. quiesces the FS). */
	struct gfs2_glock *sd_trans_gl;	/* Transaction glock structure */

	/* Inode Stuff */

	struct gfs2_inode *sd_master_dir;
	struct gfs2_inode *sd_jindex;
	struct gfs2_inode *sd_inum_inode;
	struct gfs2_inode *sd_statfs_inode;
	struct gfs2_inode *sd_ir_inode;
	struct gfs2_inode *sd_sc_inode;
	struct gfs2_inode *sd_ut_inode;
	struct gfs2_inode *sd_qc_inode;
	struct gfs2_inode *sd_rindex;	    /* Resource Index (rindex) inode */
	struct gfs2_inode *sd_quota_inode;   /* Special on-disk quota file */
	struct gfs2_inode *sd_root_dir;

	/* Inum stuff */

	struct semaphore sd_inum_mutex;

	/* StatFS stuff */

	spinlock_t sd_statfs_spin;
	struct semaphore sd_statfs_mutex;
	struct gfs2_statfs_change sd_statfs_master;
	struct gfs2_statfs_change sd_statfs_local;
	unsigned long sd_statfs_sync_time;

	/* Resource group stuff */

	uint64_t sd_rindex_vn;	        /* Resource Index version # (detects
	                                   whether new rgrps have been added) */
	spinlock_t sd_rindex_spin;
	struct semaphore sd_rindex_mutex;
	struct list_head sd_rindex_list;/* List of all resource groups,
					   on-disk order */
	struct list_head sd_rindex_mru_list;/* List of all resource groups,
					   most-recently-used (MRU) order */
	struct list_head sd_rindex_recent_list;	/* List of rgrps from which blocks
					   were recently allocated */
	struct gfs2_rgrpd *sd_rindex_forward;/* Next rgrp from which to attempt
					   a block alloc */

	unsigned int sd_rgrps;	/* Total # of resource groups */

	/* Journal index stuff */

	struct list_head sd_jindex_list;
	spinlock_t sd_jindex_spin;
	struct semaphore sd_jindex_mutex;
	unsigned int sd_journals;
	unsigned long sd_jindex_refresh_time;

	struct gfs2_jdesc *sd_jdesc;
	struct gfs2_holder sd_journal_gh;
	struct gfs2_holder sd_jinode_gh;

	struct gfs2_holder sd_ir_gh;
	struct gfs2_holder sd_sc_gh;
	struct gfs2_holder sd_ut_gh;
	struct gfs2_holder sd_qc_gh;

	/* Daemon stuff */

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

	/*
	 * Unlinked inode stuff.
	 * List includes newly created, not-yet-linked inodes,
	 *   as well as inodes that have been unlinked and are waiting
         *   to be de-allocated.
	 */
	struct list_head sd_unlinked_list; /* List of unlinked inodes */
	atomic_t sd_unlinked_count;
	spinlock_t sd_unlinked_spin;
	struct semaphore sd_unlinked_mutex;

	unsigned int sd_unlinked_slots;
	unsigned int sd_unlinked_chunks;
	unsigned char **sd_unlinked_bitmap;

	/* Quota stuff */

	struct list_head sd_quota_list; /* List of unlinked inodes */
	atomic_t sd_quota_count;
	spinlock_t sd_quota_spin;
	struct semaphore sd_quota_mutex;

	unsigned int sd_quota_slots;
	unsigned int sd_quota_chunks;
	unsigned char **sd_quota_bitmap;

	uint64_t sd_quota_sync_gen;     /* Generation, incr when sync to file */
	unsigned long sd_quota_sync_time; /* Jiffies, last sync to quota file */

	/* Log stuff */

	spinlock_t sd_log_lock;
	atomic_t sd_log_trans_count;
	wait_queue_head_t sd_log_trans_wq;
	atomic_t sd_log_flush_count;
	wait_queue_head_t sd_log_flush_wq;

	unsigned int sd_log_blks_reserved;
	unsigned int sd_log_commited_buf;
	unsigned int sd_log_commited_revoke;

	unsigned int sd_log_num_gl;
	unsigned int sd_log_num_buf;
	unsigned int sd_log_num_revoke;
	unsigned int sd_log_num_rg;
	unsigned int sd_log_num_databuf;
	struct list_head sd_log_le_gl;
	struct list_head sd_log_le_buf;
	struct list_head sd_log_le_revoke;
	struct list_head sd_log_le_rg;
	struct list_head sd_log_le_databuf;

	unsigned int sd_log_blks_free;
	struct list_head sd_log_blks_list;
	wait_queue_head_t sd_log_blks_wait;

	uint64_t sd_log_sequence;
	unsigned int sd_log_head;
	unsigned int sd_log_tail;
	uint64_t sd_log_wraps;
	int sd_log_idle;

	unsigned long sd_log_flush_time;
	struct semaphore sd_log_flush_lock;
	struct list_head sd_log_flush_list;

	unsigned int sd_log_flush_head;
	uint64_t sd_log_flush_wrapped;

	struct list_head sd_ail1_list;
	struct list_head sd_ail2_list;
	uint64_t sd_ail_sync_gen;

	/* Replay stuff */

	struct list_head sd_revoke_list;
	unsigned int sd_replay_tail;

	unsigned int sd_found_blocks;
	unsigned int sd_found_revokes;
	unsigned int sd_replayed_blocks;

	/* For quiescing the filesystem */

	struct gfs2_holder sd_freeze_gh;
	struct semaphore sd_freeze_lock;
	unsigned int sd_freeze_count;

	/* Counters */

	/* current quantities of various things */
	atomic_t sd_glock_count;      /* # of gfs2_glock structs alloc'd */
	atomic_t sd_glock_held_count; /* # of glocks locked by this node */
	atomic_t sd_inode_count;      /* # of gfs2_inode structs alloc'd */
	atomic_t sd_bufdata_count;    /* # of gfs2_bufdata structs alloc'd */

	atomic_t sd_fh2dentry_misses; /* total # get_dentry misses */
	atomic_t sd_reclaimed;        /* total # glocks reclaimed since mount */
	atomic_t sd_log_flush_incore;
	atomic_t sd_log_flush_ondisk;

	/* total lock-related calls handled since mount */
	atomic_t sd_glock_nq_calls;
	atomic_t sd_glock_dq_calls;
	atomic_t sd_glock_prefetch_calls;
	atomic_t sd_lm_lock_calls;
	atomic_t sd_lm_unlock_calls;
	atomic_t sd_lm_callbacks;

	atomic_t sd_bio_reads;
	atomic_t sd_bio_writes;
	atomic_t sd_bio_outstanding;

	/* total calls from Linux VFS handled since mount */
	atomic_t sd_ops_address;
	atomic_t sd_ops_dentry;
	atomic_t sd_ops_export;
	atomic_t sd_ops_file;
	atomic_t sd_ops_inode;
	atomic_t sd_ops_super;
	atomic_t sd_ops_vm;

	char sd_fsname[256];

	/* Debugging crud */

	unsigned long sd_last_warning;

	struct list_head sd_list;
};

#endif /* __INCORE_DOT_H__ */
