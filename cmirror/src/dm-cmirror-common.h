/*
 * Copyright (C) 2005 Red Hat, Inc.
 *
 * This file is released under the LGPL.
 */

#ifndef __DM_CMIRROR_COMMON_H__
#define __DM_CMIRROR_COMMON_H__

/*
 * Magic for persistent mirrors: "MiRr"
 */
#define MIRROR_MAGIC 0x4D695272

/*
 * The on-disk version of the metadata.
 */
#define MIRROR_DISK_VERSION 1
#define LOG_OFFSET 2
#define MAX_NAME_LEN 128

struct log_header {
	uint32_t magic;

	/*
	 * Simple, incrementing version. no backward
	 * compatibility.
	 */
	uint32_t version;
	sector_t nr_regions;
};

struct log_c {
	struct dm_target *ti;
	int touched;
	sector_t region_size;
	region_t region_count;
	region_t sync_count;

	unsigned bitset_uint32_count;
	uint32_t *clean_bits;
	uint32_t *sync_bits;
	uint32_t *recovering_bits;	/* FIXME: this seems excessive */

	int sync_search;

	/* Resync flag */
	enum sync {
		DEFAULTSYNC,	/* Synchronize if necessary */
		NOSYNC,		/* Devices known to be already in sync */
		FORCESYNC,	/* Force a sync to happen */
	} sync;

	/*
	 * Disk log fields
	 */
	int log_dev_failed;
	struct dm_dev *log_dev;
	struct log_header header;

	struct io_region header_location;
	struct log_header *disk_header;

	struct io_region bits_location;
	uint32_t *disk_bits;

	/*
	 * Cluster log fields
	 */
	char uuid[MAX_NAME_LEN];
	int paranoid;
	atomic_t in_sync;  /* like sync_count, except all or nothing */
	atomic_t suspend;

	struct list_head log_list;
	struct list_head region_users;

	uint32_t server_id;
	struct socket *client_sock;
};

#define suspend_on(wq, sleep_cond) \
do \
{ \
	DECLARE_WAITQUEUE(__wait_chan, current); \
	current->state = TASK_UNINTERRUPTIBLE; \
	add_wait_queue(wq, &__wait_chan); \
	if ((sleep_cond)){ \
		printk("suspend_on:: " #wq "\n"); \
		schedule(); \
	} \
	remove_wait_queue(wq, &__wait_chan); \
	current->state = TASK_RUNNING; \
} \
while (0)

#endif /*__DM_CMIRROR_COMMON_H__ */
/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
