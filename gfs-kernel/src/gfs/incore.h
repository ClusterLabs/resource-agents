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

#ifndef __INCORE_DOT_H__
#define __INCORE_DOT_H__

#define DIO_NEW           (0x00000001)
#define DIO_FORCE         (0x00000002)
#define DIO_CLEAN         (0x00000004)
#define DIO_DIRTY         (0x00000008)
#define DIO_START         (0x00000010)
#define DIO_WAIT          (0x00000020)
#define DIO_METADATA      (0x00000040)
#define DIO_DATA          (0x00000080)
#define DIO_INVISIBLE     (0x00000100)
#define DIO_CHECK         (0x00000200)
#define DIO_ALL           (0x00000400)

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

	/*  Operations that happen at recovery time  */

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

	char *lo_name;
};

/*
 *  Structure that gets added to struct gfs_trans->tr_elements.  They
 *  make up the "stuff" in each transaction.
 */

struct gfs_log_element {
	struct gfs_log_operations *le_ops;

	struct gfs_trans *le_trans;
	struct list_head le_list;
};

struct gfs_meta_header_cache {
	struct list_head mc_list_hash;
	struct list_head mc_list_single;
	struct list_head mc_list_rgd;

	uint64_t mc_block;
	struct gfs_meta_header mc_mh;
};

struct gfs_depend {
	struct list_head gd_list_hash;
	struct list_head gd_list_rgd;

	struct gfs_rgrpd *gd_rgd;
	uint64_t gd_formal_ino;
	unsigned long gd_time;
};

/*
 *  Structure containing information about the allocation bitmaps.
 *  There are one of these for each fs block that the bitmap for
 *  the resource group header covers.
 */

struct gfs_bitmap {
	uint32_t bi_offset;	/* The offset in the buffer of the first byte */
	uint32_t bi_start;	/* The position of the first byte in this block */
	uint32_t bi_len;	/* The number of bytes in this block */
};

/*
 *  Structure containing information Resource Groups
 */

struct gfs_rgrpd {
	struct list_head rd_list;	/* Link with superblock */
	struct list_head rd_list_mru;
	struct list_head rd_recent;	/* Recently used rgrps */

	struct gfs_glock *rd_gl;	/* Glock for rgrp */

	unsigned long rd_flags;

	struct gfs_rindex rd_ri;	/* Resource Index structure */
	struct gfs_rgrp rd_rg;	        /* Resource Group structure */
	uint64_t rd_rg_vn;

	struct gfs_bitmap *rd_bits;
	struct buffer_head **rd_bh;

	uint32_t rd_last_alloc_data;
	uint32_t rd_last_alloc_meta;

	struct list_head rd_mhc;
	struct list_head rd_depend;

	struct gfs_sbd *rd_sbd;
};

/*
 *  Per-buffer data
 */

struct gfs_bufdata {
	struct buffer_head *bd_bh;	/* struct buffer_head which this struct belongs to */
	struct gfs_glock *bd_gl;	/* Pointer to Glock struct for this bh */

	struct gfs_log_element bd_new_le;
	struct gfs_log_element bd_incore_le;

	char *bd_frozen;
	struct semaphore bd_lock;

	unsigned int bd_pinned;	                /* Pin count */
	struct list_head bd_ail_tr_list;	/* List of buffers hanging off tr_ail_bufs */
	struct list_head bd_ail_gl_list;	/* List of buffers hanging off gl_ail_bufs */
};

/*
 *  Glock operations
 */

struct gfs_glock_operations {
	void (*go_xmote_th) (struct gfs_glock * gl, unsigned int state,
			     int flags);
	void (*go_xmote_bh) (struct gfs_glock * gl);
	void (*go_drop_th) (struct gfs_glock * gl);
	void (*go_drop_bh) (struct gfs_glock * gl);
	void (*go_sync) (struct gfs_glock * gl, int flags);
	void (*go_inval) (struct gfs_glock * gl, int flags);
	int (*go_demote_ok) (struct gfs_glock * gl);
	int (*go_lock) (struct gfs_glock * gl, int flags);
	void (*go_unlock) (struct gfs_glock * gl, int flags);
	void (*go_callback) (struct gfs_glock * gl, unsigned int state);
	void (*go_greedy) (struct gfs_glock * gl);
	int go_type;
};

/*  Actions  */
#define HIF_MUTEX               (0)
#define HIF_PROMOTE             (1)
#define HIF_DEMOTE              (2)
#define HIF_GREEDY              (3)

/*  States  */
#define HIF_ALLOCED             (4)
#define HIF_DEALLOC             (5)
#define HIF_HOLDER              (6)
#define HIF_FIRST               (7)
#define HIF_WAKEUP              (8)
#define HIF_RECURSE             (9)

struct gfs_holder {
	struct list_head gh_list;

	struct gfs_glock *gh_gl;
	struct task_struct *gh_owner;
	unsigned int gh_state;
	int gh_flags;

	int gh_error;
	unsigned long gh_iflags;
	struct completion gh_wait;
};

/*
 *  Glock Structure
 */

#define GLF_PLUG                (0)
#define GLF_LOCK                (1)
#define GLF_STICKY              (2)
#define GLF_PREFETCH            (3)
#define GLF_SYNC                (4)
#define GLF_DIRTY               (5)
#define GLF_LVB_INVALID         (6)
#define GLF_SKIP_WAITERS2       (7)
#define GLF_GREEDY              (8)

struct gfs_glock {
	struct list_head gl_list;
	unsigned long gl_flags;
	struct lm_lockname gl_name;
	atomic_t gl_count;

	spinlock_t gl_spin;

	unsigned int gl_state;
	struct list_head gl_holders;
	struct list_head gl_waiters1;	/*  HIF_MUTEX  */
	struct list_head gl_waiters2;	/*  HIF_DEMOTE, HIF_GREEDY  */
	struct list_head gl_waiters3;	/*  HIF_PROMOTE  */

	struct gfs_glock_operations *gl_ops;

	struct gfs_holder *gl_req_gh;
	gfs_glop_bh_t gl_req_bh;

	lm_lock_t *gl_lock;
	char *gl_lvb;
	atomic_t gl_lvb_count;

	uint64_t gl_vn;
	unsigned long gl_stamp;
	void *gl_object;

	struct gfs_log_element gl_new_le;
	struct gfs_log_element gl_incore_le;

	struct gfs_gl_hash_bucket *gl_bucket;
	struct list_head gl_reclaim;

	struct gfs_sbd *gl_sbd;

	struct inode *gl_aspace;
	struct list_head gl_dirty_buffers;
	struct list_head gl_ail_bufs;
};

/*
 *  In-Place Reservation structure
 */

struct gfs_alloc {
	/*  Quota stuff  */

	unsigned int al_qd_num;
	struct gfs_quota_data *al_qd[4];
	struct gfs_holder al_qd_ghs[4];

	/* Filled in by the caller to gfs_inplace_reserve() */

	uint32_t al_requested_di;
	uint32_t al_requested_meta;
	uint32_t al_requested_data;

	/* Filled in by gfs_inplace_reserve() */

	char *al_file;
	unsigned int al_line;
	struct gfs_holder al_ri_gh;
	struct gfs_holder al_rgd_gh;
	struct gfs_rgrpd *al_rgd;
	uint32_t al_reserved_meta;
	uint32_t al_reserved_data;

	/* Filled in by gfs_blkalloc() */

	uint32_t al_alloced_di;
	uint32_t al_alloced_meta;
	uint32_t al_alloced_data;

	/* Dinode allocation crap */

	struct gfs_unlinked *al_ul;
};

/*
 *  Incore inode structure
 */

#define GIF_QD_LOCKED           (0)
#define GIF_PAGED               (1)
#define GIF_SW_PAGED            (2)

struct gfs_inode {
	struct gfs_inum i_num;

	atomic_t i_count;
	unsigned long i_flags;

	uint64_t i_vn;
	struct gfs_dinode i_di;

	struct gfs_glock *i_gl;
	struct gfs_sbd *i_sbd;
	struct inode *i_vnode;

	struct gfs_holder i_iopen_gh;

	struct gfs_alloc *i_alloc;
	uint64_t i_last_rg_alloc;

	struct task_struct *i_creat_task;
	pid_t i_creat_pid;

	spinlock_t i_lock;
	struct buffer_head *i_cache[GFS_MAX_META_HEIGHT];

	unsigned int i_greedy;
	unsigned long i_last_pfault;
};

/*
 *  GFS per-fd structure
 */

#define GFF_DID_DIRECT_ALLOC    (0)

struct gfs_file {
	unsigned long f_flags;

	struct semaphore f_fl_lock;
	struct gfs_holder f_fl_gh;

	struct gfs_inode *f_inode;
	struct file *f_vfile;
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
	struct list_head ul_list;
	unsigned int ul_count;

	struct gfs_inum ul_inum;
	unsigned long ul_flags;

	struct gfs_log_element ul_new_le;
	struct gfs_log_element ul_incore_le;
	struct gfs_log_element ul_ondisk_le;
};

/*
 *  Quota log element
 */

struct gfs_quota_le {
	struct gfs_log_element ql_le;

	struct gfs_quota_data *ql_data;
	struct list_head ql_data_list;

	int64_t ql_change;
};

#define QDF_USER                (0)
#define QDF_OD_LIST             (1)
#define QDF_LOCK                (2)

struct gfs_quota_data {
	struct list_head qd_list;
	unsigned int qd_count;

	uint32_t qd_id;
	unsigned long qd_flags;

	struct list_head qd_le_list;

	int64_t qd_change_new;
	int64_t qd_change_ic;
	int64_t qd_change_od;
	int64_t qd_change_sync;

	struct gfs_quota_le qd_ondisk_ql;
	uint64_t qd_sync_gen;

	struct gfs_glock *qd_gl;
	struct gfs_quota_lvb qd_qb;

	unsigned long qd_last_warn;
};

struct gfs_log_buf {
	struct list_head lb_list;

	struct buffer_head lb_bh;
	struct buffer_head *lb_unlock;
};

/*
 *  Transaction structures
 */

#define TRF_LOG_DUMP            (0x00000001)

struct gfs_trans {
	struct list_head tr_list;

	/* Initial creation stuff */

	char *tr_file;
	unsigned int tr_line;

	unsigned int tr_mblks_asked;	/* Number of log blocks asked to be reserved */
	unsigned int tr_eblks_asked;
	unsigned int tr_seg_reserved;	/* Number of segments reserved */

	struct gfs_holder *tr_t_gh;

	/* Stuff filled in during creation */

	unsigned int tr_flags;
	struct list_head tr_elements;

	/* Stuff modified during the commit */

	unsigned int tr_num_free_bufs;
	struct list_head tr_free_bufs;
	unsigned int tr_num_free_bmem;
	struct list_head tr_free_bmem;

	uint64_t tr_log_head;	        /* The current log head */
	uint64_t tr_first_head;	        /* First header block */

	struct list_head tr_bufs;	/* List of buffers going to the log */

	/* Stuff that's part of the AIL */

	struct list_head tr_ail_bufs;

	/* Private data for different log element types */

	unsigned int tr_num_gl;
	unsigned int tr_num_buf;
	unsigned int tr_num_iul;
	unsigned int tr_num_ida;
	unsigned int tr_num_q;
};

/*
 *  One bucket of the glock hash table.
 */

struct gfs_gl_hash_bucket {
	rwlock_t hb_lock;
	struct list_head hb_list;
} __attribute__ ((__aligned__(SMP_CACHE_BYTES)));

/*
 *  Super Block Data Structure  (One per filesystem)
 */

#define SDF_JOURNAL_LIVE        (0)
#define SDF_SCAND_RUN           (1)
#define SDF_GLOCKD_RUN          (2)
#define SDF_RECOVERD_RUN        (3)
#define SDF_LOGD_RUN            (4)
#define SDF_QUOTAD_RUN          (5)
#define SDF_INODED_RUN          (6)
#define SDF_NOATIME             (7)
#define SDF_ROFS                (8)
#define SDF_NEED_LOG_DUMP       (9)
#define SDF_FOUND_UL_DUMP       (10)
#define SDF_FOUND_Q_DUMP        (11)
#define SDF_IN_LOG_DUMP         (12)

#define GFS_GL_HASH_SHIFT       (13)
#define GFS_GL_HASH_SIZE        (1 << GFS_GL_HASH_SHIFT)
#define GFS_GL_HASH_MASK        (GFS_GL_HASH_SIZE - 1)

#define GFS_MHC_HASH_SHIFT      (10)
#define GFS_MHC_HASH_SIZE       (1 << GFS_MHC_HASH_SHIFT)
#define GFS_MHC_HASH_MASK       (GFS_MHC_HASH_SIZE - 1)

#define GFS_DEPEND_HASH_SHIFT   (10)
#define GFS_DEPEND_HASH_SIZE    (1 << GFS_DEPEND_HASH_SHIFT)
#define GFS_DEPEND_HASH_MASK    (GFS_DEPEND_HASH_SIZE - 1)

struct gfs_sbd {
	struct gfs_sb sd_sb;	        /* Super Block */

	struct super_block *sd_vfs;	/* FS's device independent sb */

	struct gfs_args sd_args;
	unsigned long sd_flags;

	struct gfs_tune sd_tune;	/* FS tuning structure */

	/* Resource group stuff */

	struct gfs_inode *sd_riinode;	/* rindex inode */
	uint64_t sd_riinode_vn;	/* Version number of the resource index inode */

	struct list_head sd_rglist;	/* List of resource groups */
	struct semaphore sd_rindex_lock;

	struct list_head sd_rg_mru_list;	/* List of resource groups in MRU order */
	spinlock_t sd_rg_mru_lock;	/* Lock for MRU list */
	struct list_head sd_rg_recent;	/* Recently used rgrps */
	spinlock_t sd_rg_recent_lock;
	struct gfs_rgrpd *sd_rg_forward;	/* Next new rgrp to try for allocation */
	spinlock_t sd_rg_forward_lock;

	unsigned int sd_rgcount;	/* Count of resource groups */

	/*  Constants computed on mount  */

	uint32_t sd_fsb2bb;
	uint32_t sd_fsb2bb_shift;	/* Shift FS Block numbers to the left by
					   this to get buffer cache blocks  */
	uint32_t sd_diptrs;	/* Number of pointers in a dinode */
	uint32_t sd_inptrs;	/* Number of pointers in a indirect block */
	uint32_t sd_jbsize;	/* Size of a journaled data block */
	uint32_t sd_hash_bsize;	/* sizeof(exhash block) */
	uint32_t sd_hash_bsize_shift;
	uint32_t sd_hash_ptrs;	/* Number of points in a hash block */
	uint32_t sd_max_dirres;	/* Maximum space needed to add a directory entry */
	uint32_t sd_max_height;	/* Maximum height of a file's metadata tree */
	uint64_t sd_heightsize[GFS_MAX_META_HEIGHT];
	uint32_t sd_max_jheight;	/* Maximum height of a journaled file's metadata tree */
	uint64_t sd_jheightsize[GFS_MAX_META_HEIGHT];

	/*  Lock Stuff  */

	struct gfs_gl_hash_bucket sd_gl_hash[GFS_GL_HASH_SIZE];

	struct list_head sd_reclaim_list;
	spinlock_t sd_reclaim_lock;
	wait_queue_head_t sd_reclaim_wchan;
	atomic_t sd_reclaim_count;

	struct lm_lockstruct sd_lockstruct;

	struct list_head sd_mhc[GFS_MHC_HASH_SIZE];
	struct list_head sd_mhc_single;
	spinlock_t sd_mhc_lock;
	atomic_t sd_mhc_count;

	struct list_head sd_depend[GFS_DEPEND_HASH_SIZE];
	spinlock_t sd_depend_lock;
	atomic_t sd_depend_count;

	struct gfs_holder sd_live_gh;

	struct gfs_holder sd_freeze_gh;
	struct semaphore sd_freeze_lock;
	unsigned int sd_freeze_count;

	/*  Inode Stuff  */

	struct gfs_inode *sd_rooti;	/* FS's root inode */

	struct gfs_glock *sd_rename_gl;	/* rename glock */

	/*  Daemon stuff  */

	struct task_struct *sd_scand_process;
	unsigned int sd_glockd_num;
	struct task_struct *sd_recoverd_process;
	struct task_struct *sd_logd_process;
	struct task_struct *sd_quotad_process;
	struct task_struct *sd_inoded_process;

	struct semaphore sd_thread_lock;
	struct completion sd_thread_completion;

	/*  Log stuff  */

	struct gfs_glock *sd_trans_gl;	/* transaction glock */

	struct gfs_inode *sd_jiinode;	/* jindex inode */
	uint64_t sd_jiinode_vn;	/* Version number of the journal index inode */

	unsigned int sd_journals;	/* Number of journals in the FS */
	struct gfs_jindex *sd_jindex;	/* Array of Jindex structures describing this FS's journals */
	struct semaphore sd_jindex_lock;
	unsigned long sd_jindex_refresh_time;

	struct gfs_jindex sd_jdesc;	/* Jindex structure describing this machine's journal */
	struct gfs_holder sd_journal_gh;	/* the glock for this machine's journal */

	uint64_t sd_sequence;	/* Assigned to xactions in order they commit */
	uint64_t sd_log_head;	/* Block number of next journal write */
	uint64_t sd_log_wrap;

	spinlock_t sd_log_seg_lock;
	unsigned int sd_log_seg_free;	/* Free segments in the log */
	struct list_head sd_log_seg_list;
	wait_queue_head_t sd_log_seg_wait;

	struct list_head sd_log_ail;	/* struct gfs_trans structures that form the Active Items List 
					   "next" is the head, "prev" is the tail  */

	struct list_head sd_log_incore;	/* transactions that have been commited incore (but not ondisk)
					   "next" is the newest, "prev" is the oldest  */
	unsigned int sd_log_buffers;	/* Number of buffers in the incore log */

	struct semaphore sd_log_lock;	/* Lock for access to log values */

	uint64_t sd_log_dump_last;
	uint64_t sd_log_dump_last_wrap;

	/*  unlinked crap  */

	struct list_head sd_unlinked_list;
	spinlock_t sd_unlinked_lock;

	atomic_t sd_unlinked_ic_count;
	atomic_t sd_unlinked_od_count;

	/*  quota crap  */

	struct list_head sd_quota_list;
	spinlock_t sd_quota_lock;

	atomic_t sd_quota_count;
	atomic_t sd_quota_od_count;

	struct gfs_inode *sd_qinode;

	uint64_t sd_quota_sync_gen;
	unsigned long sd_quota_sync_time;

	/*  license crap  */

	struct gfs_inode *sd_linode;

	/*  Recovery stuff  */

	struct list_head sd_dirty_j;
	spinlock_t sd_dirty_j_lock;

	unsigned int sd_recovery_replays;
	unsigned int sd_recovery_skips;
	unsigned int sd_recovery_sames;

	/*  Counters  */

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

	/*  Debugging crud  */

	unsigned long sd_last_readdirplus;
	unsigned long sd_last_unlocked_aop;

	spinlock_t sd_ail_lock;
	struct list_head sd_recovery_bufs;
};

#endif /* __INCORE_DOT_H__ */
