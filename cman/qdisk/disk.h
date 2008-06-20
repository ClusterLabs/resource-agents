/**
  @file Main quorum daemon include file
 */
#ifndef _QUORUM_DISK_H
#define _QUORUM_DISK_H

#include <stdint.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <libcman.h>

#define MAX_NODES_DISK		16	
#define MEMB_MASK_LEN           ((MAX_NODES_DISK / 8) + \
				 (!!(MAX_NODES_DISK % 8)))
#define DISK_MEMB_MASK_LEN	((MEMB_MASK_LEN + 7) & ~7)

/** The membership bitmask type */
typedef uint8_t memb_mask_t [DISK_MEMB_MASK_LEN];

typedef enum {
	S_NONE  = 0x0,		// Shutdown / not quorate / not running
	S_EVICT	= 0x1,		// Voted out / about to be fenced.
	/* ^^^ Fencing OK */
	S_INIT	= 0x2,		// Initializing.  Hold your fire.
        /* vvv Fencing will kill a node */
	S_RUN	= 0x5,		// I think I'm running.
	S_MASTER= 0x6		// I know I'm running, and have advertised to
				// CMAN the availability of the disk vote for my
				// partition.
} disk_node_state_t;


typedef enum {
	M_NONE  = 0x0,
	M_BID	= 0x1,
	M_ACK	= 0x2,
	M_NACK	= 0x3,
	M_MASK	= 0x4
} disk_msg_id_t;


typedef enum {
	FL_MSG	= 0x1,
	FL_BID	= 0x2,
	FL_VOTE = 0x4
} disk_state_flag_t;


typedef enum {
	RF_REBOOT = 0x1,		/* Reboot if we go from master->none */
	RF_STOP_CMAN = 0x2,
	RF_DEBUG = 0x4,
	RF_PARANOID = 0x8,
	RF_ALLOW_KILL = 0x10,
	RF_UPTIME = 0x20,
	RF_CMAN_LABEL = 0x40
} run_flag_t;


/* RHEL 2.1 / RHCS3 old magic numbers */
#define HEADER_MAGIC_OLD	0x39119FCD	/* partition header */
#define STATE_MAGIC_OLD		0xF1840DCE	/* Status block */
#define SHARED_HEADER_MAGIC_OLD	0x00DEBB1E	/* Per-block header */

/* Conversion */
#define HEADER_MAGIC_NUMBER	0xeb7a62c2	/* Partition header */
#define STATE_MAGIC_NUMBER	0x47bacef8	/* Status block */
#define SHARED_HEADER_MAGIC	0x00DEBB1E	/* Per-block headeer */

/* Version magic. */
#define VERSION_MAGIC_V2	0x389fabc4


typedef struct __attribute__ ((packed)) {
	uint32_t	ps_magic;
	/* 4 */
	uint32_t	ps_updatenode;		// Last writer
	/* 8 */
	uint64_t	ps_timestamp;		// time of last update
	/* 16 */
	uint32_t	ps_nodeid;
	uint32_t	pad0;
	/* 24 */
	uint8_t		ps_state;		// running or stopped
	uint8_t		pad1[1];
	uint16_t	ps_flags;
	/* 26 */
	uint16_t	ps_score;		// Local points
	uint16_t	ps_scoremax;		// What we think is our max
						// points, if other nodes
						// disagree, we may be voted
						// out
	/* 28 */
	uint32_t	ps_ca_sec;		// Cycle speed (average)
	uint32_t	ps_ca_usec;
	/* 36 */
	uint32_t	ps_lc_sec;		// Cycle speed (last)
	uint32_t	ps_lc_usec;
	uint64_t	ps_incarnation;		// Token to detect hung +
						// restored node
	/* 44 */
	uint16_t	ps_msg;			// Vote/bid mechanism 
	uint16_t	ps_seq;
	uint32_t	ps_arg;
	/* 52 */
	memb_mask_t	ps_mask;		// Bitmap
	memb_mask_t	ps_master_mask;		// Bitmap
	/* 60 */
} status_block_t;

#define swab_status_block_t(ptr) \
{\
	swab32((ptr)->ps_magic);\
	swab32((ptr)->ps_updatenode);\
	swab64((ptr)->ps_timestamp);\
	swab32((ptr)->ps_nodeid);\
	swab32((ptr)->pad0);\
	/* state + pad */ \
	swab16((ptr)->ps_flags);\
	swab16((ptr)->ps_score);\
	swab16((ptr)->ps_scoremax);\
	/* Cycle speeds */ \
	swab32((ptr)->ps_ca_sec);\
	swab32((ptr)->ps_ca_usec);\
	swab32((ptr)->ps_lc_sec);\
	swab32((ptr)->ps_lc_usec);\
	/* Message */ \
	swab16((ptr)->ps_msg); \
	swab16((ptr)->ps_seq); \
	swab32((ptr)->ps_arg); \
 }


/*
 * Shared state disk header.  Describes cluster global information.
 */
typedef struct __attribute__ ((packed)) {
	uint32_t	qh_magic;
	uint32_t	qh_version;	   // 
	uint64_t	qh_timestamp;	   // time of last update
	char 		qh_updatehost[128];// Hostname who put this here...
	char		qh_cluster[120];   // Cluster name; CMAN only 
					   // supports 16 chars.
	uint32_t	qh_blksz;          // Known block size @ creation
	uint32_t	qh_kernsz;	   // Ingored
} quorum_header_t;

#define swab_quorum_header_t(ptr) \
{\
	swab32((ptr)->qh_magic); \
	swab32((ptr)->qh_version); \
	swab32((ptr)->qh_blksz); \
	swab32((ptr)->qh_kernsz); \
	swab64((ptr)->qh_timestamp); \
}



/*
 * The user data is stored with this header prepended.
 * The header ONLY contains CRC information and the length of the data.
 * The data blocks themselves contain their own respective magic numbers.
 */
typedef struct __attribute__ ((packed)) {
	uint32_t h_magic;		/* Header magic	       */
	uint32_t h_hcrc;		/* Header CRC          */
	uint32_t h_dcrc;		/* CRC32 of data       */
	uint32_t h_length;		/* Length of real data */
	uint64_t h_view;		/* View # of real data */
	uint64_t h_timestamp;		/* Timestamp           */
} shared_header_t;

#define SHARED_HEADER_INITIALIZER = {0, 0, 0, 0, 0, 0}

#define swab_shared_header_t(ptr) \
{\
	swab32((ptr)->h_magic);\
	swab32((ptr)->h_hcrc);\
	swab32((ptr)->h_dcrc);\
	swab32((ptr)->h_length);\
	swab64((ptr)->h_view);\
	swab64((ptr)->h_timestamp);\
}


/* Offsets from RHCM 1.2.x */
#define OFFSET_HEADER	0
#define HEADER_SIZE(ssz)		(ssz<4096?4096:ssz)

#define OFFSET_FIRST_STATUS_BLOCK(ssz)	(OFFSET_HEADER + HEADER_SIZE(ssz))
#define SPACE_PER_STATUS_BLOCK(ssz)	(ssz<4096?4096:ssz)
#define STATUS_BLOCK_COUNT		MAX_NODES_DISK

#define END_OF_DISK(ssz)		(OFFSET_FIRST_STATUS_BLOCK(ssz) + \
					 (MAX_NODES_DISK + 1) * \
					 SPACE_PER_STATUS_BLOCK(ssz)) \


typedef struct {
	int d_fd;
	int _pad_;
	size_t d_blksz;
	size_t d_pagesz;
} target_info_t;


/* From disk.c */
int qdisk_open(char *name, target_info_t *disk);
int qdisk_close(target_info_t *disk);
int qdisk_init(char *name, char *clustername);
int qdisk_validate(char *name);
int qdisk_read(target_info_t *disk, __off64_t ofs, void *buf, int len);
int qdisk_write(target_info_t *disk, __off64_t ofs, const void *buf, int len);

#define qdisk_nodeid_offset(nodeid, ssz) \
	(OFFSET_FIRST_STATUS_BLOCK(ssz) + (SPACE_PER_STATUS_BLOCK(ssz) * (nodeid - 1)))

/* From disk_utils.c */
#define HISTORY_LENGTH 60
typedef struct {
	disk_msg_id_t m_msg;	 /* this is an int, but will be stored as 16bit*/
	uint32_t m_arg;
	uint16_t m_seq;
	uint16_t pad0;
} disk_msg_t;


typedef struct {
	uint64_t qc_incarnation;
	struct timeval qc_average;
	struct timeval qc_last[HISTORY_LENGTH];
	target_info_t qc_disk;
	int qc_my_id;
	int qc_writes;
	int qc_interval;
	int qc_tko;
	int qc_tko_up;
	int qc_upgrade_wait;
	int qc_master_wait;
	int qc_votes;
	int qc_scoremin;
	int qc_sched;
	int qc_sched_prio;
	disk_node_state_t qc_disk_status;
	disk_node_state_t qc_status;
	int qc_master;		/* Master?! */
	int qc_status_sock;
	run_flag_t qc_flags;
	cman_handle_t qc_ch;
	char *qc_device;
	char *qc_label;
	char *qc_status_file;
	char *qc_cman_label;
	char *qc_status_sockname;
} qd_ctx;

typedef struct {
	uint64_t ni_incarnation;
	uint64_t ni_evil_incarnation;
	time_t	ni_last_seen;
	int	ni_misses;
	int	ni_seen;
	disk_msg_t ni_msg;
	disk_msg_t ni_last_msg;
	disk_node_state_t ni_state;
	status_block_t ni_status;
} node_info_t;

int qd_write_status(qd_ctx *ctx, int nid, disk_node_state_t state,
		    disk_msg_t *msg, memb_mask_t mask, memb_mask_t master);
int qd_init(qd_ctx *ctx, cman_handle_t ch, int me);
void qd_destroy(qd_ctx *ctx);

/* proc.c */
int find_partitions(const char *label,
		    char *devname, size_t devlen, int print);
int check_device(char *device, char *label, quorum_header_t *qh, int flags);


#endif
