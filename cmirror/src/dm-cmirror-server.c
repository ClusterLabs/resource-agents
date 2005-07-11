/*
 * Copyright (C) 2005 Red Hat, Inc.
 *
 * This file is released under the LGPL.
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/list.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/socket.h>
#include <linux/signal.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/device-mapper.h>
#include <cluster/service.h>
#include <cluster/cnxman.h>
#include <cluster/cnxman-socket.h>

#include "dm-log.h"
#include "dm-cmirror-common.h"
#include "dm-cmirror-xfr.h"
#include "dm-cmirror-cman.h"

#define RU_READ    0
#define RU_WRITE   1
#define RU_RECOVER 2

struct region_user {
	struct list_head ru_list;
	int32_t  ru_rw;
	uint32_t ru_nodeid;
	region_t ru_region;
};

static mempool_t *region_user_pool;

static atomic_t server_run;
static struct completion server_completion;

static wait_queue_head_t _suspend_queue;
static atomic_t _suspend;

static int debug_disk_write = 0;
extern struct list_head log_list_head;

static void *region_user_alloc(unsigned int gfp_mask, void *pool_data){
	return kmalloc(sizeof(struct region_user), gfp_mask);
}

static void region_user_free(void *element, void *pool_data){
	kfree(element);
}

/*
 * The touched member needs to be updated every time we access
 * one of the bitsets.
 */
static inline int log_test_bit(uint32_t *bs, unsigned bit)
{
	return test_bit(bit, (unsigned long *) bs) ? 1 : 0;
}

static inline void log_set_bit(struct log_c *l,
			       uint32_t *bs, unsigned bit)
{
	set_bit(bit, (unsigned long *) bs);
	l->touched = 1;
}

static inline void log_clear_bit(struct log_c *l,
				 uint32_t *bs, unsigned bit)
{
	clear_bit(bit, (unsigned long *) bs);
	l->touched = 1;
}

/*----------------------------------------------------------------
 * Header IO
 *--------------------------------------------------------------*/
static void header_to_disk(struct log_header *core, struct log_header *disk)
{
	disk->magic = cpu_to_le32(core->magic);
	disk->version = cpu_to_le32(core->version);
	disk->nr_regions = cpu_to_le64(core->nr_regions);
}

static void header_from_disk(struct log_header *core, struct log_header *disk)
{
	core->magic = le32_to_cpu(disk->magic);
	core->version = le32_to_cpu(disk->version);
	core->nr_regions = le64_to_cpu(disk->nr_regions);
}

static int read_header(struct log_c *log)
{
	int r;
	unsigned long ebits;

	r = dm_io_sync_vm(1, &log->header_location, READ,
			  log->disk_header, &ebits);
	if (unlikely(r))
		return r;

	header_from_disk(&log->header, log->disk_header);

	/* New log required? */
	if (log->sync != DEFAULTSYNC || log->header.magic != MIRROR_MAGIC) {
		log->header.magic = MIRROR_MAGIC;
		log->header.version = MIRROR_DISK_VERSION;
		log->header.nr_regions = 0;
	}

	if (log->header.version != MIRROR_DISK_VERSION) {
		DMWARN("incompatible disk log version");
		return -EINVAL;
	}

	return 0;
}

static inline int write_header(struct log_c *log)
{
	unsigned long ebits;

	header_to_disk(&log->header, log->disk_header);
	return dm_io_sync_vm(1, &log->header_location, WRITE,
			     log->disk_header, &ebits);
}

/*----------------------------------------------------------------
 * Bits IO
 *--------------------------------------------------------------*/
static inline void zeros_to_core(uint32_t *core, unsigned count)
{
	memset(core, 0, sizeof(uint32_t)*count);
}

static inline void bits_to_core(uint32_t *core, uint32_t *disk, unsigned count)
{
	unsigned i;

	for (i = 0; i < count; i++)
		core[i] = le32_to_cpu(disk[i]);
}

static inline void zeros_to_disk(uint32_t *disk, unsigned count)
{
	memset(disk, 0, sizeof(uint32_t)*count);
}

static inline void bits_to_disk(uint32_t *core, uint32_t *disk, unsigned count)
{
	unsigned i;

	/* copy across the clean/dirty bitset */
	for (i = 0; i < count; i++)
		disk[i] = cpu_to_le32(core[i]);
}

static int read_bits(struct log_c *log)
{
	int r;
	unsigned long ebits;

	r = dm_io_sync_vm(1, &log->bits_location, READ,
			  log->disk_bits, &ebits);

	if (unlikely(r))
		return r;

	bits_to_core(log->clean_bits, log->disk_bits,
		     log->bitset_uint32_count);
	return 0;
}

static int write_bits(struct log_c *log)
{
	unsigned long ebits;
	
	bits_to_disk(log->clean_bits, log->disk_bits,
		     log->bitset_uint32_count);
	
	return dm_io_sync_vm(1, &log->bits_location, WRITE,
			      log->disk_bits, &ebits);
}

static int count_bits32(uint32_t *addr, unsigned size)
{
	int count = 0, i;

	for (i = 0; i < size; i++) {
		count += hweight32(*(addr+i));
	}
	return count;
}

static int _core_get_resync_work(struct log_c *lc, region_t *region)
{
	if (lc->sync_search >= lc->region_count){
		return 0;
	}
	do {
		*region = find_next_zero_bit((unsigned long *) lc->sync_bits,
					     lc->region_count,
					     lc->sync_search);
		lc->sync_search = *region + 1;

		if (*region >= lc->region_count)
			return 0;

	} while (log_test_bit(lc->recovering_bits, *region));

	log_set_bit(lc, lc->recovering_bits, *region);
	return 1;
}


static int print_zero_bits(unsigned char *str, int offset, int bit_count){
	int i,j;
	int count=0;
	int len = bit_count/8 + ((bit_count%8)?1:0);
	int region = offset;
	int range_count=0;

	for(i = 0; i < len; i++){
		if(str[i] == 0x0){
			region+=(bit_count < 8)? bit_count: 8;
			range_count+= (bit_count < 8)? bit_count: 8;
			count+=(bit_count < 8)? bit_count: 8;

			bit_count -= (bit_count < 8)? bit_count: 8;
			continue;
		} else if(str[i] == 0xFF){
			if(range_count==1){
				DMINFO("  %d", region - 1);
			} else if(range_count){
				DMINFO("  %d - %d", region-range_count, region-1);
			}
			range_count = 0;
			region+=(bit_count < 8)? bit_count: 8;      

			bit_count -= (bit_count < 8)? bit_count: 8;
			continue;
		}
		for(j=0; j<8; j++){
			if(!bit_count--){
				break;
			}
			if(!(str[i] & 1<<j)){
				range_count++;
				region++;
				count++;
			} else {
				if(range_count==1){
					DMINFO("  %d", region - 1);
				} else if(range_count){
					DMINFO("  %d - %d", region-range_count, region-1);
				}
				range_count = 0;
				region++;
			}
		}
	}

	if(range_count==1){
		DMINFO("  %d", region - 1);
	} else if(range_count){
		DMINFO("  %d - %d", region-range_count, region);
	}
	return count;
}

static void fail_log_device(struct log_c *lc)
{
	lc->log_dev_failed = 1;
	dm_table_event(lc->ti->table);
}

static void restore_log_device(struct log_c *lc)
{
	lc->log_dev_failed = 0;
}

static int disk_resume(struct log_c *lc)
{
	int r;
	int good_count=0, bad_count=0;
	unsigned i;
	size_t size = lc->bitset_uint32_count * sizeof(uint32_t);
	struct region_user *tmp_ru, *ru;
	unsigned char live_nodes[16]; /* Attention -- max of 128 nodes... */

	DMINFO("Disk Resume::");

	debug_disk_write = 1;

	memset(live_nodes, 0, sizeof(live_nodes));
	for(i = 0; i < global_count; i++){
		live_nodes[global_nodeids[i]/8] |= 1 << (global_nodeids[i]%8);
	}

	/* read the disk header */
	i = 1;
	if ((r = read_header(lc)) || (i = 0) || (r = read_bits(lc))) {
		if (r == -EINVAL)
			return r;

		DMWARN("Read %s failed on mirror log device, %s",
		       i ? "header" : "bits", lc->log_dev->name);
		fail_log_device(lc);
		lc->header.nr_regions = 0;
	}

	/* set or clear any new bits */
	if (lc->sync == NOSYNC)
		for (i = lc->header.nr_regions; i < lc->region_count; i++)
			/* FIXME: amazingly inefficient */
			log_set_bit(lc, lc->clean_bits, i);
	else
		for (i = lc->header.nr_regions; i < lc->region_count; i++)
			/* FIXME: amazingly inefficient */
			log_clear_bit(lc, lc->clean_bits, i);

	/* clear any unused bits */
	for(i = lc->region_count; i < lc->bitset_uint32_count*32; i++)
		log_clear_bit(lc, lc->clean_bits, i);

	/* copy clean across to sync */
	memcpy(lc->sync_bits, lc->clean_bits, size);

	/* must go through the list twice.  The dead node could have been using **
	** the same region as other nodes and we want any region that was in    **
	** use by the dead node to be marked _not_ in-sync..................... */
	list_for_each_entry(ru, &lc->region_users, ru_list){
		if(live_nodes[ru->ru_nodeid/8] & 1 << (ru->ru_nodeid%8)){
			good_count++;
			log_set_bit(lc, lc->sync_bits, ru->ru_region);
		}
	}

	list_for_each_entry_safe(ru, tmp_ru, &lc->region_users, ru_list){
		if(!(live_nodes[ru->ru_nodeid/8] & 1 << (ru->ru_nodeid%8))){
			bad_count++;
			log_clear_bit(lc, lc->sync_bits, ru->ru_region);
			if (ru->ru_rw == RU_RECOVER) {
				log_clear_bit(lc, lc->recovering_bits, ru->ru_region);
			}
			list_del(&ru->ru_list);
			mempool_free(ru, region_user_pool);
		}
	}

	DMINFO("  Live nodes        :: %d", global_count);
	DMINFO("  In-Use Regions    :: %d", good_count+bad_count);
	DMINFO("  Good IUR's        :: %d", good_count);
	DMINFO("  Bad IUR's         :: %d", bad_count);

	lc->sync_count = count_bits32(lc->sync_bits, lc->bitset_uint32_count);

	DMINFO("  Sync count        :: %Lu", lc->sync_count);
	DMINFO("  Disk Region count :: %Lu", lc->header.nr_regions);
	DMINFO("  Region count      :: %Lu", lc->region_count);

	if(lc->header.nr_regions != lc->region_count){
		DMINFO("  NOTE:  Mapping has changed.");
	}
/* Take this out for now.
	if(list_empty(&lc->region_users) && (lc->sync_count != lc->header.nr_regions)){
		struct region_user *new;
		
		for(sync_search = 0; sync_search < lc->header.nr_regions;){
			region = find_next_zero_bit((unsigned long *)lc->clean_bits,
						    lc->header.nr_regions,
						    sync_search);
			sync_search = region+1;
			if(region < lc->header.nr_regions){
				for(i=0; i < global_count; i++){
					new = kmalloc(sizeof(struct region_user),
						      GFP_KERNEL);
					if(!new){
						DMERR("Unable to allocate space to track region users.");
						BUG();
					}
					new->ru_nodeid = global_nodeids[i];
					new->ru_region = region;
					DMINFO("Adding %u/%Lu",
					       new->ru_nodeid, new->ru_region);
					list_add(&new->ru_list, &lc->region_users);
				}
			}
		}
	}			

*/
	DMINFO("Marked regions::");
	i = print_zero_bits((unsigned char *)lc->clean_bits, 0, lc->header.nr_regions);
	DMINFO("  Total = %d", i);

	DMINFO("Out-of-sync regions::");
	i = print_zero_bits((unsigned char *)lc->sync_bits, 0, lc->header.nr_regions);
	DMINFO("  Total = %d", i);

	/* set the correct number of regions in the header */
	lc->header.nr_regions = lc->region_count;

	i = 1;
	if ((r = write_bits(lc)) || (i = 0) || (r = write_header(lc))) {
		DMWARN("Write %s failed on mirror log device, %s.",
		       i ? "bits" : "header", lc->log_dev->name);
		fail_log_device(lc);
	} else 
		restore_log_device(lc);
/* ATTENTION -- fixme 
	atomic_set(&lc->suspended, 0);
*/
	return r;
}


struct region_user *find_ru(struct log_c *lc, uint32_t who, region_t region){
	struct region_user *ru;
	list_for_each_entry(ru, &lc->region_users, ru_list){
		if((who == ru->ru_nodeid) && (region == ru->ru_region)){
			return ru;
		}
	}
	return NULL;
}

struct region_user *find_ru_by_region(struct log_c *lc, region_t region){
	struct region_user *ru;
	list_for_each_entry(ru, &lc->region_users, ru_list){
		if(region == ru->ru_region){
			return ru;
		}
	}
	return NULL;
}


static int server_is_clean(struct log_c *lc, struct log_request *lr)
{
	lr->u.lr_int_rtn = log_test_bit(lc->clean_bits, lr->u.lr_region);

	return 0;
}

static int server_is_remote_recovering(struct log_c *lc, struct log_request *lr)
{
	struct region_user *ru;

	if ((ru = find_ru_by_region(lc, lr->u.lr_region)) && 
	    (ru->ru_rw == RU_RECOVER))
		lr->u.lr_int_rtn = 1;
	else
		lr->u.lr_int_rtn = 0;

	return 0;
}

static int server_in_sync(struct log_c *lc, struct log_request *lr)
{
	if(likely(log_test_bit(lc->sync_bits, lr->u.lr_region)))
		/* in-sync */
		lr->u.lr_int_rtn = 1;
	else
		lr->u.lr_int_rtn = 0;

	return 0;
}


static int server_mark_region(struct log_c *lc, struct log_request *lr, uint32_t who)
{
	int r = 0;
	struct region_user *ru, *new;

	new = mempool_alloc(region_user_pool, GFP_KERNEL);
	if(!new){
		return -ENOMEM;
	}

	new->ru_nodeid = who;
	new->ru_region = lr->u.lr_region;
    
	if (!(ru = find_ru_by_region(lc, lr->u.lr_region))) {
		log_clear_bit(lc, lc->clean_bits, lr->u.lr_region);
		r = write_bits(lc);

		list_add(&new->ru_list, &lc->region_users);
		if (!r) {
			lc->touched = 0;
			restore_log_device(lc);
		} else {
			DMERR("Write bits failed on mirror log device, %s",
			      lc->log_dev->name);
			/* ATTENTION -- need to halt here 
			if (!atomic_read(&lc->suspended))
				wait_for_completion(&lc->failure_completion);
			*/
		}
	} else if (ru->ru_rw == RU_RECOVER) {
		DMERR("Attempt to mark a region " SECTOR_FORMAT "which is being recovered.",
		      lr->u.lr_region);
		DMERR("Current recoverer: %u", ru->ru_nodeid);
		DMERR("Mark requester   : %u", who);
		mempool_free(new, region_user_pool);
		return -EBUSY;
	} else if (!find_ru(lc, who, lr->u.lr_region)) {
		list_add(&new->ru_list, &ru->ru_list);
	} else {
		DMWARN("Attempt to mark a already marked region (%u,"
		       SECTOR_FORMAT
		       ")",
		       who, lr->u.lr_region);
		mempool_free(new, region_user_pool);
	}

	return 0;
}


static int server_clear_region(struct log_c *lc, struct log_request *lr, uint32_t who)
{
	struct region_user *ru;

	ru = find_ru(lc, who, lr->u.lr_region);
	if(!ru){
		DMWARN("request to remove unrecorded region user (%u/%Lu)",
		       who, lr->u.lr_region);
		/*
		** ATTENTION -- may not be there because it is trying to clear **
		** a region it is resyncing, not because it marked it.  Need   **
		** more care ? */
		/*return -EINVAL;*/
	} else if (ru->ru_rw != RU_RECOVER) {
		list_del(&ru->ru_list);
		mempool_free(ru, region_user_pool);
	} else {
		DMWARN("Clearing recovering region...");
	}

	if(!find_ru_by_region(lc, lr->u.lr_region)){
		log_set_bit(lc, lc->clean_bits, lr->u.lr_region);
		if (!write_bits(lc)) {
			DMERR("Write bits failed on mirror log device, %s",
			      lc->log_dev->name);
		}
	}
	return 0;
}


static int server_get_resync_work(struct log_c *lc, struct log_request *lr, uint32_t who)
{
	struct region_user *new;

	new = mempool_alloc(region_user_pool, GFP_KERNEL);
	if(!new){
		return -ENOMEM;
	}
	
	if ((lr->u.lr_int_rtn = _core_get_resync_work(lc, &(lr->u.lr_region_rtn)))){
		new->ru_nodeid = who;
		new->ru_region = lr->u.lr_region;
		new->ru_rw = RU_RECOVER;
		list_add(&new->ru_list, &lc->region_users);
	} else {
		mempool_free(new, region_user_pool);
	}

	return 0;
}


static int server_complete_resync_work(struct log_c *lc, struct log_request *lr){
	struct region_user *ru;
	uint32_t info;

	log_clear_bit(lc, lc->recovering_bits, lr->u.lr_region);
	log_set_bit(lc, lc->sync_bits, lr->u.lr_region);
	lc->sync_count++;

	info = (uint32_t)(lc->region_count - lc->sync_count);

	if((info < 10001 && !(info%1000)) ||
	   (info < 1000 && !(info%100)) ||
	   (info < 200 && !(info%25)) ||
	   (info < 6)){
		DMINFO(SECTOR_FORMAT " out-of-sync regions remaining.",
		       lc->region_count - lc->sync_count);
	}

	ru = find_ru_by_region(lc, lr->u.lr_region);
	if (!ru) {
		DMERR("complete_resync_work attempt on unrecorded region.");
	} else if (ru->ru_rw != RU_RECOVER){
		DMERR("complete_resync_work attempt on non-recovering region.");
	} else {
		list_del(&ru->ru_list);
		mempool_free(ru,region_user_pool);
	}
	return 0;
}


static int server_get_sync_count(struct log_c *lc, struct log_request *lr){
	lr->u.lr_region_rtn = lc->sync_count;
	return 0;
}


static struct log_c *get_log_context(char *uuid){
	struct log_c *lc;

	list_for_each_entry(lc, &log_list_head, log_list){
		if(!strncmp(lc->uuid, uuid, MAX_NAME_LEN)){
			return lc;
		}
	}

	return NULL;
}


static int process_election(struct log_request *lr, struct log_c *lc,
			    struct sockaddr_in *saddr){
	int i;
	uint32_t lowest, next;
	uint32_t node_count=global_count, *nodeids=global_nodeids;

	/* Record the starter's port number so we can get back to him */
	if((lr->u.lr_starter == my_id) && (!lr->u.lr_node_count)){
		lr->u.lr_starter_port = saddr->sin_port;
	}

	/* Find the next node id in the circle */
	for(lowest = my_id, next = my_id, i=0; i < node_count; i++){
		if(lowest > nodeids[i]){
			lowest = nodeids[i];
		}
		if(((next == my_id) || (next > nodeids[i])) &&
		   (nodeids[i] > my_id)){
			next = nodeids[i];
		}
	}

	/* Set address to point to next node in the circle */
	next = (next == my_id)? lowest: next;
	saddr->sin_port = CLUSTER_LOG_PORT;
	if(!(saddr->sin_addr.s_addr = nodeid_to_ipaddr(next))){
		return -1;
	}

	
	if((lr->lr_type == LRT_MASTER_LEAVING) && 
	   (lr->u.lr_starter == my_id) &&
	   lr->u.lr_node_count){
		lr->u.lr_coordinator = 0xDEAD;
		if(!(saddr->sin_addr.s_addr = nodeid_to_ipaddr(lr->u.lr_starter))){
			return -1;
		}
		saddr->sin_port = lr->u.lr_starter_port;
		return 0;
	}
	
	if(!lc){
		lr->u.lr_node_count++;
		return 0;
	}
	
	if(lc->server_id == my_id){
		lr->u.lr_coordinator = my_id;
		if(!(saddr->sin_addr.s_addr = nodeid_to_ipaddr(lr->u.lr_starter))){
			return -1;
		}
		saddr->sin_port = lr->u.lr_starter_port;
		return 0;
	}
	
	
	if(lr->lr_type == LRT_MASTER_LEAVING){
		lc->server_id = 0xDEAD;
		lr->u.lr_node_count++;
		return 0;
	}
	
	if(lr->lr_type == LRT_ELECTION){
		if((lr->u.lr_starter == my_id) && (lr->u.lr_node_count)){
			if(node_count == lr->u.lr_node_count){
				lr->lr_type = LRT_SELECTION;
			} else {
				lr->u.lr_coordinator = my_id;
			}
			lr->u.lr_node_count = 1;
			return 0;
		}

		lr->u.lr_node_count++;
		
		if(my_id < lr->u.lr_coordinator){
			lr->u.lr_coordinator = my_id;
		}
		return 0;
	} else if(lr->lr_type == LRT_SELECTION){
		if(lr->u.lr_starter != my_id){
			lr->u.lr_node_count++;
			return 0;
		}
		
		if(lr->u.lr_node_count == node_count){
			lr->lr_type = LRT_MASTER_ASSIGN;
		} else {
			lr->lr_type = LRT_ELECTION;
			lr->u.lr_coordinator = my_id;
			return 0;
		}
		lr->u.lr_node_count = 1;
	} else if(lr->lr_type == LRT_MASTER_ASSIGN){
		if(lr->u.lr_coordinator == my_id){
			lc->server_id = my_id;
		}
		if(lr->u.lr_starter != my_id){
			return 0;
		}
		if(!(saddr->sin_addr.s_addr = nodeid_to_ipaddr(lr->u.lr_starter))){
			return -1;
		}
		saddr->sin_port = lr->u.lr_starter_port;
		lc->server_id = lr->u.lr_coordinator;
	}
	return 0;
}


/**
 * process_log_request
 * @sock - the socket to receive requests on
 *
 * This function receives a region request for a specific
 * mirror set/region.  ATTENTION -- fill rest of desc.
 *
 * Returns: 0 on success, -1 on error
 */
static int process_log_request(struct socket *sock){
	int error;
	uint32_t nodeid;
	struct msghdr msg;
	struct iovec iov;
	struct sockaddr_in saddr_in;
	mm_segment_t fs;
	struct log_c *lc;
	struct log_request lr; /* ATTENTION -- could be too much on the stack */

	memset(&lr, 0, sizeof(struct log_request));
	memset(&saddr_in, 0, sizeof(saddr_in));
		
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_iovlen = 1;
	msg.msg_iov = &iov;
	msg.msg_flags = 0;
	msg.msg_name = &saddr_in;
	msg.msg_namelen = sizeof(saddr_in);
	iov.iov_len = sizeof(struct log_request);
	iov.iov_base = &lr;
		
	fs = get_fs();
	set_fs(get_ds());
		
	error = my_recvmsg(sock, &msg, sizeof(struct log_request),
			     0, 5);
	set_fs(fs);
		
	if(error > 0){
		if(error < sizeof(struct log_request)){
			DMERR("Cluster log server received incomplete message.");
		}
		lc = get_log_context(lr.lr_uuid);

		if(lr.lr_type == LRT_ELECTION ||
		   lr.lr_type == LRT_SELECTION ||
		   lr.lr_type == LRT_MASTER_ASSIGN ||
		   lr.lr_type == LRT_MASTER_LEAVING){
			uint32_t old = (lc)?lc->server_id: 0xDEAD;
			if(process_election(&lr, lc, &saddr_in)){
				DMERR("Election processing failed.");
				return -1;
			}
			if(lc && (old != lc->server_id) && (my_id == lc->server_id)){
				DMINFO("I'm the cluster log server, READING DISK");
				disk_resume(lc);
			}
			goto reply;
		}

		if(!lc){
			DMWARN("Log context can not be found for request");
			lr.u.lr_int_rtn = -ENXIO;
			goto reply;
		}

/*
  if(lc->server_id != my_id){
  DMWARN("I am not the server for this request");
  lr.u.lr_int_rtn = -ENXIO;
  goto reply;
  }
*/				

		if(atomic_read(&lc->suspend)){
			lr.u.lr_int_rtn = -EAGAIN;
			goto reply;
		}

		switch(lr.lr_type){
		case LRT_IS_CLEAN:
			error = server_is_clean(lc, &lr);
			break;
		case LRT_IS_REMOTE_RECOVERING:
			error = server_is_remote_recovering(lc, &lr);
			break;
		case LRT_IN_SYNC:
			error = server_in_sync(lc, &lr);
			break;
		case LRT_MARK_REGION:
			if(!(nodeid = 
			     ipaddr_to_nodeid((struct sockaddr *)msg.msg_name))){
				return -EINVAL;
				break;
			}
			error = server_mark_region(lc, &lr, nodeid);
			break;
		case LRT_CLEAR_REGION:
			if(!(nodeid = 
			     ipaddr_to_nodeid((struct sockaddr *)msg.msg_name))){
				return -EINVAL;
				break;
			}
			error = server_clear_region(lc, &lr, nodeid);
			break;
		case LRT_GET_RESYNC_WORK:
			if(!(nodeid = 
			     ipaddr_to_nodeid((struct sockaddr *)msg.msg_name))){
				return -EINVAL;
				break;
			}
			error = server_get_resync_work(lc, &lr, nodeid);
			break;
		case LRT_COMPLETE_RESYNC_WORK:
			error = server_complete_resync_work(lc, &lr);
			break;
		case LRT_GET_SYNC_COUNT:
			error = server_get_sync_count(lc, &lr);
			break;
		default:
			DMWARN("unknown request type received");
			return 0;  /* do not send a reply */
			break;
		}

		/* ATTENTION -- if error? */
		if(error){
			DMWARN("An error occured while processing request type %d",
			       lr.lr_type);
			lr.u.lr_int_rtn = error;
		}

	reply:
    
		/* Why do we need to reset this? */
		iov.iov_len = sizeof(struct log_request);
		iov.iov_base = &lr;
		msg.msg_name = &saddr_in;
		msg.msg_namelen = sizeof(saddr_in);

		fs = get_fs();
		set_fs(get_ds());
			
		error = sock_sendmsg(sock, &msg, sizeof(struct log_request));
			
		set_fs(fs);
		if(error < 0){
			DMWARN("unable to sendmsg to client (error = %d)", error);
			return error;
		}
	} else if(error == -EAGAIN || error == -ETIMEDOUT){
		return 0;
	} else {
		/* ATTENTION -- what do we do with this ? */
		return error;
	}
	return 0;
}


static int cluster_log_serverd(void *data){
	int error;
	struct log_c *lc;
	struct sockaddr_in saddr_in;
	struct socket *sock;

	/* read the disk logs */

	daemonize("cluster_log_serverd");

	error = sock_create(AF_INET, SOCK_DGRAM,
			    0,
			    &sock);
	if(error < 0){
		DMWARN("failed to create cluster log server socket.");
		goto fail1;
	}

	memset(&saddr_in, 0, sizeof(struct sockaddr_cl));
	saddr_in.sin_family = AF_INET;
	saddr_in.sin_port = CLUSTER_LOG_PORT;
	error = sock->ops->bind(sock, (struct sockaddr *)&saddr_in,
				sizeof(saddr_in));

	if(error < 0){
		DMWARN("failed to bind cluster log server socket.");
		goto fail2;
	}

	complete(&server_completion);
  
	for(;;){
		if(!atomic_read(&server_run)){
			DMINFO("Cluster log server recieved message to shut down.");
			break;
		}

		suspend_on(&_suspend_queue, atomic_read(&_suspend));
		switch(restart_event_type){
		case SERVICE_NODE_LEAVE:
			/* ATTENTION -- may wish to check if regions **
			** are still in use by this node.  For now,  **
			** we do the same as if the node failed.  If **
			** there are no region still in-use by the   **
			** leaving node, it won't hurt anything - and**
			** if there is, they will be recovered.      */
		case SERVICE_NODE_FAILED:
			DMINFO("A node has %s",
			       (restart_event_type == SERVICE_NODE_FAILED) ?
			       "failed." : "left the cluster.");
			
			list_for_each_entry(lc, &log_list_head, log_list){
				if(lc->server_id == my_id){
					disk_resume(lc);
				}
			}
			break;
		default:
			/* Someone has joined, or there is no event */
			break;
		}
		
		
		if(restart_event_type){
			/* finish the start phase */
			kcl_start_done(local_id, restart_event_id);
			restart_event_id = restart_event_type = 0;
		}
		
		if(process_log_request(sock)){
			DMINFO("process_log_request:: failed");
			/* ATTENTION -- what to do with error ? */
		}
		schedule();
	}

	DMINFO("Cluster log server thread is shutting down.");

	sock_release(sock);
	complete(&server_completion);
	return 0;

 fail2:
	sock_release(sock);
 fail1:
	DMWARN("Server thread failed to start");
	atomic_set(&server_run, 0);
	complete(&server_completion);
	return error;
}

void print_server_status(struct log_c *lc){
	int i;

	atomic_set(&_suspend, 1);

	DMINFO("SERVER OUTPUT::");

	DMINFO("  Live nodes        :: %d", global_count);
	DMINFO("  Sync count        :: %Lu", lc->sync_count);
	DMINFO("  Disk Region count :: %Lu", lc->header.nr_regions);
	DMINFO("  Region count      :: %Lu", lc->region_count);
	DMINFO("  nr_regions        :: %Lu", lc->header.nr_regions);
	DMINFO("  region_count      :: %Lu", lc->region_count);

	if(lc->header.nr_regions != lc->region_count){
		DMINFO("  NOTE:  Mapping has changed.");
	}

	DMINFO("Marked regions::");
	i = print_zero_bits((unsigned char *)lc->clean_bits, 0, lc->bitset_uint32_count);
	DMINFO("  Total = %d", i);

	DMINFO("Out-of-sync regions::");
	i = print_zero_bits((unsigned char *)lc->sync_bits, 0, lc->bitset_uint32_count);
	DMINFO("  Total = %d", i);

	atomic_set(&_suspend, 0);
	wake_up_all(&_suspend_queue);
}


int suspend_server(void){
	atomic_set(&_suspend, 1);
	return 0;
}

int resume_server(void){
	atomic_set(&_suspend, 0);
	wake_up_all(&_suspend_queue);
	return 0;
}

int start_server(void /* log_devices ? */){
	int error;

	region_user_pool = mempool_create(100, region_user_alloc,
					  region_user_free, NULL);
	if(!region_user_pool){
		DMWARN("unable to allocate region user pool for server");
		return -ENOMEM;
	}

	init_waitqueue_head(&_suspend_queue);

	atomic_set(&server_run, 1);
	init_completion(&server_completion);

	error = kernel_thread(cluster_log_serverd, NULL, 0);
	if(error < 0){
		DMWARN("failed to start kernel thread.");
		return error;
	}
	wait_for_completion(&server_completion);

	if(!atomic_read(&server_run)){
		DMWARN("Cluster log server thread failed to start");
		return -1;
	}
	return 0;
}


void stop_server(void){
	atomic_set(&server_run, 0);

	wait_for_completion(&server_completion);
}
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
