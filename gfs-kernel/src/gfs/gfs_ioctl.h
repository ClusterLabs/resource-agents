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

#ifndef __GFS_IOCTL_DOT_H__
#define __GFS_IOCTL_DOT_H__

#define GFS_IOCTL_VERSION (0)

#define _GFSC_(x)               (('G' << 8) | (x))

/*
   Ioctls implemented

   Reserved Ioctls:  3, 7, 8, 9, 10, 4, 13
   Next Ioctl:  44
   */

#define GFS_STACK_PRINT         _GFSC_(40)

#define GFS_GET_META            _GFSC_(31)
#define GFS_FILE_STAT           _GFSC_(30)

#define GFS_SHRINK              _GFSC_(5)

#define GFS_GET_ARGS            _GFSC_(29)
#define GFS_GET_LOCKSTRUCT      _GFSC_(39)
#define GFS_GET_SUPER           _GFSC_(19)
#define GFS_JREAD               _GFSC_(23)
#define GFS_JWRITE              _GFSC_(24)
#define GFS_JSTAT               _GFSC_(20)
#define GFS_JTRUNC              _GFSC_(33)

#define GFS_LOCK_DUMP           _GFSC_(11)

#define GFS_STATGFS             _GFSC_(12)

#define GFS_FREEZE              _GFSC_(14)
#define GFS_UNFREEZE            _GFSC_(15)

#define GFS_RECLAIM_METADATA    _GFSC_(16)

#define GFS_QUOTA_SYNC          _GFSC_(17)
#define GFS_QUOTA_REFRESH       _GFSC_(18)
#define GFS_QUOTA_READ          _GFSC_(32)

#define GFS_GET_TUNE            _GFSC_(21)
#define GFS_SET_TUNE            _GFSC_(22)

#define GFS_EATTR_GET           _GFSC_(26)
#define GFS_EATTR_SET           _GFSC_(27)

#define GFS_WHERE_ARE_YOU       _GFSC_(35)

#define GFS_SET_FLAG            _GFSC_(36)
#define GFS_CLEAR_FLAG          _GFSC_(37)

#define GFS_GET_COUNTERS        _GFSC_(43)

#define GFS_FILE_FLUSH          _GFSC_(42)

struct gfs_user_buffer {
	char *ub_data;
	unsigned int ub_size;
	unsigned int ub_count;
};

/*  Structure for jread/jwrite  */

#define GFS_HIDDEN_JINDEX       (0x10342345)
#define GFS_HIDDEN_RINDEX       (0x10342346)
#define GFS_HIDDEN_QUOTA        (0x10342347)
#define GFS_HIDDEN_LICENSE      (0x10342348)

struct gfs_jio {
	unsigned int jio_file;

	uint32_t jio_size;
	uint64_t jio_offset;
	char *jio_data;

	uint32_t jio_count;
};

/*  Structure for better GFS-specific df  */

struct gfs_usage {
	unsigned int gu_block_size;
	uint64_t gu_total_blocks;
	uint64_t gu_free;
	uint64_t gu_used_dinode;
	uint64_t gu_free_dinode;
	uint64_t gu_used_meta;
	uint64_t gu_free_meta;
};

struct gfs_reclaim_stats {
	uint64_t rc_inodes;
	uint64_t rc_metadata;
};

struct gfs_quota_name {
	int qn_user;
	uint32_t qn_id;
};

/*
 *  You can tune a filesystem, but you can't tune a yak.
 */

#define GFS_TUNE_VERSION ((GFS_IOCTL_VERSION << 16) | (139))

struct gfs_tune {
	unsigned int gt_tune_version;

	unsigned int gt_ilimit1;
	unsigned int gt_ilimit1_tries;
	unsigned int gt_ilimit1_min;
	unsigned int gt_ilimit2;
	unsigned int gt_ilimit2_tries;
	unsigned int gt_ilimit2_min;
	unsigned int gt_demote_secs;
	unsigned int gt_incore_log_blocks;
	unsigned int gt_jindex_refresh_secs;
	unsigned int gt_depend_secs;
	unsigned int gt_scand_secs;
	unsigned int gt_recoverd_secs;
	unsigned int gt_logd_secs;
	unsigned int gt_quotad_secs;
	unsigned int gt_inoded_secs;
	unsigned int gt_quota_simul_sync;
	unsigned int gt_quota_warn_period;
	unsigned int gt_atime_quantum;
	unsigned int gt_quota_quantum;
	unsigned int gt_quota_scale_num;
	unsigned int gt_quota_scale_den;
	unsigned int gt_quota_enforce;
	unsigned int gt_quota_account;
	unsigned int gt_new_files_jdata;
	unsigned int gt_new_files_directio;
	unsigned int gt_max_atomic_write;
	unsigned int gt_max_readahead;
	unsigned int gt_lockdump_size;
	unsigned int gt_stall_secs;
	unsigned int gt_complain_secs;
	unsigned int gt_reclaim_limit;
	unsigned int gt_entries_per_readdir;
	unsigned int gt_prefetch_secs;
	unsigned int gt_statfs_slots;
	unsigned int gt_max_mhc;
	unsigned int gt_greedy_default;
	unsigned int gt_greedy_quantum;
	unsigned int gt_greedy_max;
};

/*
 * Extended Attribute Ioctl structures
 *
 * Note: The name_len does not include a null character.
 *
 * Getting and setting EAs return the following errors that aren't
 * what they seem
 *
 * ENODATA - No such extended attribute
 * ERANGE - Extended attribute data is too large for the buffer
 * ENOSPC - No space left for extended attributes
 * EEXIST - Extended attribute already exists
 */

#define GFS_EACMD_SET       (0)
#define GFS_EACMD_CREATE    (1)
#define GFS_EACMD_REPLACE   (2)
#define GFS_EACMD_REMOVE    (3)

struct gfs_eaget_io {
	char *eg_data;
	char *eg_name;
	char *eg_len;
	uint32_t eg_data_len;
	uint8_t eg_name_len;
	uint8_t eg_type;	/* GFS_EATYPE_... */
};

struct gfs_easet_io {
	const char *es_data;
	char *es_name;
	uint16_t es_data_len;
	uint8_t es_name_len;	/* not counting the NULL */
	uint8_t es_cmd;		/* GFS_EACMD_...  */
	uint8_t es_type;	/* GFS_EATYPE_... */
};

#define GFS_GLOCKD_DEFAULT (1)
#define GFS_GLOCKD_MAX (32)

struct gfs_args {
	char ar_lockproto[256];	/* The name of the Lock Protocol */
	char ar_locktable[256];	/* The name of the Lock Table */
	char ar_hostdata[256];	/* The host specific data */

	int ar_ignore_local_fs;	/* Ignore the local_fs field in the struct lm_lockops */
	int ar_localflocks;	/* let the VFS do flock|fcntl locks for us */
	int ar_localcaching;	/* Local-style caching (dangerous on mulithost) */

	int ar_upgrade;		/* Upgrade ondisk/multihost format */

	unsigned int ar_num_glockd;

	int ar_posixacls;	/* Enable posix acls */
	int ar_suiddir;         /* suiddir support */
};

#endif /* ___GFS_IOCTL_DOT_H__ */
