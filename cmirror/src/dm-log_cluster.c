/*
 * Copyright (C) 2003 Sistina Software
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
#include <linux/timer.h>
#include <linux/signal.h>
#include <cluster/service.h>
#include <cluster/cnxman.h>
#include <cluster/cnxman-socket.h>

#include "dm-log.h"
#include "dm-io.h"


static int count_bits32(uint32_t *addr, unsigned size);
/******************************************************************
 ******************************************************************
 ** BEGIN COPIED CODE
 **
 ** This section of this file is taken from <kernel>/drivers/md/dm-log.c
 ** It contains some unique additions, but is mostly just copied.
 ** Search on "END COPIED CODE" to find cluster specific work.
 ******************************************************************
 *****************************************************************/


/*-----------------------------------------------------------------
 * Persistent and core logs share a lot of their implementation.
 * FIXME: need a reload method to be called from a resume
 *---------------------------------------------------------------*/
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

static int debug_disk_write=0;
static struct log_c *real_lc=NULL;



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
	if (r)
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
static inline void bits_to_core(uint32_t *core, uint32_t *disk, unsigned count)
{
	unsigned i;

	for (i = 0; i < count; i++)
		core[i] = le32_to_cpu(disk[i]);
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
	if (r)
		return r;

	bits_to_core(log->clean_bits, log->disk_bits,
		     log->bitset_uint32_count);
	return 0;
}

static int write_bits(struct log_c *log)
{
	int error;
	unsigned long ebits;
	bits_to_disk(log->clean_bits, log->disk_bits,
		     log->bitset_uint32_count);
	error = dm_io_sync_vm(1, &log->bits_location, WRITE,
			      log->disk_bits, &ebits);
	if(!debug_disk_write){
		printk("WRITING BITS BEFORE THEY'VE BEEN READ!!!\n");
	}
	if(error){
		DMERR("Failed to write bits to disk, error = %d", error);
	}
	return error;
}

/*----------------------------------------------------------------
 * core log constructor/destructor
 *
 * argv contains region_size followed optionally by [no]sync
 *--------------------------------------------------------------*/
#define BYTE_SHIFT 3
static int core_ctr(struct dirty_log *log, struct dm_target *ti,
		    unsigned int argc, char **argv)
{
	enum sync sync = DEFAULTSYNC;

	struct log_c *lc;
	sector_t region_size;
	unsigned int region_count;
	size_t bitset_size;

	if (argc < 1 || argc > 2) {
		DMWARN("wrong number of arguments to mirror log");
		return -EINVAL;
	}

	if (argc > 1) {
		if (!strcmp(argv[1], "sync"))
			sync = FORCESYNC;
		else if (!strcmp(argv[1], "nosync"))
			sync = NOSYNC;
		else {
			DMWARN("unrecognised sync argument to mirror log: %s",
			       argv[1]);
			return -EINVAL;
		}
	}

	if (sscanf(argv[0], SECTOR_FORMAT, &region_size) != 1) {
		DMWARN("invalid region size string");
		return -EINVAL;
	}

	region_count = dm_div_up(ti->len, region_size);

	lc = kmalloc(sizeof(*lc), GFP_KERNEL);
	real_lc = lc;
	if (!lc) {
		DMWARN("couldn't allocate core log");
		return -ENOMEM;
	}

	lc->ti = ti;
	lc->touched = 0;
	lc->region_size = region_size;
	lc->region_count = region_count;
	lc->sync = sync;

	/*
	 * Work out how many words we need to hold the bitset.
	 */
	bitset_size = dm_round_up(region_count,
				  sizeof(*lc->clean_bits) << BYTE_SHIFT);
	bitset_size >>= BYTE_SHIFT;

	lc->bitset_uint32_count = bitset_size / 4;
	lc->clean_bits = vmalloc(bitset_size);
	if (!lc->clean_bits) {
		DMWARN("couldn't allocate clean bitset");
		kfree(lc);
		return -ENOMEM;
	}
	memset(lc->clean_bits, -1, bitset_size);

	lc->sync_bits = vmalloc(bitset_size);
	if (!lc->sync_bits) {
		DMWARN("couldn't allocate sync bitset");
		vfree(lc->clean_bits);
		kfree(lc);
		return -ENOMEM;
	}
	memset(lc->sync_bits, (sync == NOSYNC) ? -1 : 0, bitset_size);
	lc->sync_count = (sync == NOSYNC) ? region_count : 0;

	lc->recovering_bits = vmalloc(bitset_size);
	if (!lc->recovering_bits) {
		DMWARN("couldn't allocate sync bitset");
		vfree(lc->sync_bits);
		vfree(lc->clean_bits);
		kfree(lc);
		return -ENOMEM;
	}
	memset(lc->recovering_bits, 0, bitset_size);
	lc->sync_search = 0;
	log->context = lc;
	return 0;
}

static void core_dtr(struct dirty_log *log)
{
	struct log_c *lc = (struct log_c *) log->context;
	vfree(lc->clean_bits);
	vfree(lc->sync_bits);
	vfree(lc->recovering_bits);
	kfree(lc);
}

/*----------------------------------------------------------------
 * disk log constructor/destructor
 *
 * argv contains log_device region_size followed optionally by [no]sync
 *--------------------------------------------------------------*/
static int disk_ctr(struct dirty_log *log, struct dm_target *ti,
		    unsigned int argc, char **argv)
{
	int r;
	size_t size;
	struct log_c *lc;
	struct dm_dev *dev;

	if (argc < 2 || argc > 3) {
		DMWARN("wrong number of arguments to disk mirror log");
		return -EINVAL;
	}

	r = dm_get_device(ti, argv[0], 0, 0 /* FIXME */,
			  FMODE_READ | FMODE_WRITE, &dev);
	if (r){
		DMWARN("Unable to get device %s\n", argv[0]);
		return r;
	}
	r = core_ctr(log, ti, argc - 1, argv + 1);
	if (r) {
		dm_put_device(ti, dev);
		return r;
	}

	lc = (struct log_c *) log->context;
	lc->log_dev = dev;

	/* setup the disk header fields */
	lc->header_location.bdev = lc->log_dev->bdev;
	lc->header_location.sector = 0;
	lc->header_location.count = 1;

	/*
	 * We can't read less than this amount, even though we'll
	 * not be using most of this space.
	 */
	lc->disk_header = vmalloc(1 << SECTOR_SHIFT);
	if (!lc->disk_header)
		goto bad;

	/* setup the disk bitset fields */
	lc->bits_location.bdev = lc->log_dev->bdev;
	lc->bits_location.sector = LOG_OFFSET;

	size = dm_round_up(lc->bitset_uint32_count * sizeof(uint32_t),
			   1 << SECTOR_SHIFT);
	lc->bits_location.count = size >> SECTOR_SHIFT;
	lc->disk_bits = vmalloc(size);
	if (!lc->disk_bits) {
		vfree(lc->disk_header);
		goto bad;
	}
	return 0;

 bad:
	dm_put_device(ti, lc->log_dev);
	core_dtr(log);
	return -ENOMEM;
}

static void disk_dtr(struct dirty_log *log)
{
	struct log_c *lc = (struct log_c *) log->context;
	dm_put_device(lc->ti, lc->log_dev);
	vfree(lc->disk_header);
	vfree(lc->disk_bits);
	core_dtr(log);
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


/******************************************************************
 ******************************************************************
 ** END COPIED CODE
 ******************************************************************
 *****************************************************************/


/********************************************************
 ** Cluster dirty log type
 *******************************************************/

/*************************************
 **  Common macros and structures
 ************************************/
static unsigned int request_count=0;
static unsigned int request_retry_count=0;


#define LRT_IS_CLEAN			0x1
#define LRT_IN_SYNC             	0x2
#define LRT_MARK_REGION         	0x4
#define LRT_GET_RESYNC_WORK     	0x8
#define LRT_GET_SYNC_COUNT      	0x10
#define LRT_CLEAR_REGION        	0x20
#define LRT_COMPLETE_RESYNC_WORK        0x40

#define LRT_ELECTION			0x80
#define LRT_SELECTION			0x100
#define LRT_MASTER_ASSIGN		0x200
#define LRT_MASTER_LEAVING		0x400

#define CLUSTER_LOG_PORT 51005

LIST_HEAD(log_list_head);

struct log_request {
	int lr_type;
	union {
		struct {
			uint32_t lr_starter;
			int lr_starter_port;
			uint32_t lr_node_count;
			uint32_t lr_coordinator;
		};
		struct {
			int lr_int_rtn;          /* use this if int return */
			region_t lr_region_rtn;  /* use this if region_t return */
			sector_t lr_region;
		};
	} u;
	char lr_uuid[MAX_NAME_LEN];
};

static uint32_t local_id, my_id=0;

/* these are used by the server and updated by the service ops **
** We are not in danger when updating them, because the server **
** will be suspended while they are being updated............. */
static int global_count=0;
static uint32_t *global_nodeids=NULL;

static int restart_event_type=0;
static int restart_event_id=0;

#define suspend_on(wq, sleep_cond) \
do \
{ \
	DECLARE_WAITQUEUE(__wait_chan, current); \
	current->state = TASK_UNINTERRUPTIBLE; \
	add_wait_queue(wq, &__wait_chan); \
	if ((sleep_cond)){ \
		printk("suspending.\n"); \
		schedule(); \
	} \
	remove_wait_queue(wq, &__wait_chan); \
	current->state = TASK_RUNNING; \
} \
while (0)
static wait_queue_head_t suspend_client_queue;
static wait_queue_head_t suspend_server_queue;
static atomic_t suspend_client;
static atomic_t suspend_server;


/*************************************
 **  Server macros and structures
 ************************************/
struct region_user {
	struct list_head ru_list;
	uint32_t ru_nodeid;
	region_t ru_region;
};

static atomic_t server_run;
static struct completion server_completion;

/*************************************
 **  Client macros and structures
 ************************************/
/* tracks in-use regions and regions in the process of clearing */
struct region_state {
	struct log_c *rs_lc;
	int rs_marked;
	region_t rs_region;
	struct list_head rs_list;
};

static spinlock_t region_state_lock;
static int clear_region_count=0;
static struct list_head clear_region_list;
static struct list_head marked_region_list;
static int clear_req=0;
static int mark_req=0;
static int insync_req=0;
static int clear_req2ser=0;
static int mark_req2ser=0;
static int insync_req2ser=0;

/****************************************************************
*****************************************************************
**  Common code
**
*****************************************************************
****************************************************************/
static uint32_t nodeid_to_ipaddr(uint32_t nodeid){
	struct cluster_node_addr *cna;
	struct sockaddr_in *saddr;
	struct list_head *list = kcl_get_node_addresses(nodeid);

	if(!list){
		printk("No address list for nodeid %u\n", nodeid);
		return 0;
	}
		

	list_for_each_entry(cna, list, list){
		saddr = (struct sockaddr_in *)(&cna->addr);
		return (uint32_t)(saddr->sin_addr.s_addr);
	}
	return 0;
}

static uint32_t ipaddr_to_nodeid(struct sockaddr *addr){
	struct list_head *addr_list;
	struct kcl_cluster_node node;
	struct cluster_node_addr *tmp;

	if(!(addr_list = kcl_get_node_addresses(my_id))){
		DMWARN("No address list available for %u\n", my_id);
		goto fail;
	}

	if(addr->sa_family == AF_INET){
		struct sockaddr_in a4;
		struct sockaddr_in *tmp_addr;
		list_for_each_entry(tmp, addr_list, list){
			tmp_addr = (struct sockaddr_in *)tmp->addr;
			if(tmp_addr->sin_family == AF_INET){
				memcpy(&a4, tmp_addr, sizeof(a4));
				memcpy(&a4.sin_addr,
				       &((struct sockaddr_in *)addr)->sin_addr,
				       sizeof(a4.sin_addr));
				if(!kcl_get_node_by_addr((char *)&a4,
							 sizeof(a4),
							 &node)){
					return node.node_id;
				}
			}
		}
	} else if(addr->sa_family == AF_INET6){
		struct sockaddr_in6 a6;
		struct sockaddr_in6 *tmp_addr;
		list_for_each_entry(tmp, addr_list, list){
			tmp_addr = (struct sockaddr_in6 *)tmp->addr;
			if(tmp_addr->sin6_family == AF_INET6){
				memcpy(&a6, tmp_addr, sizeof(a6));
				memcpy(&a6.sin6_addr,
				       &((struct sockaddr_in6 *)addr)->sin6_addr,
				       sizeof(a6.sin6_addr));
				if(!kcl_get_node_by_addr((char *)&a6,
							 sizeof(a6),
							 &node)){
					return node.node_id;
				}
			}
		}
	}

 fail:
	DMWARN("Failed to convert IP address to nodeid.");
	return 0;
}

static void set_sigusr1(unsigned long arg){
        struct task_struct *tsk = (struct task_struct *)arg;
        send_sig(SIGUSR1, tsk, 0);
}

static int my_recvmsg(struct socket *sock, struct msghdr *msg,
		      size_t size, int flags, int time_out){
	int rtn;
	unsigned long sig_flags;
	sigset_t blocked_save;
	struct timer_list timer = TIMER_INITIALIZER(set_sigusr1,
						    jiffies+(time_out*HZ),
						    (unsigned long)current);

        spin_lock_irqsave(&current->sighand->siglock, sig_flags);
        blocked_save = current->blocked;
        sigdelsetmask(&current->blocked, sigmask(SIGUSR1));
        recalc_sigpending();
        spin_unlock_irqrestore(&current->sighand->siglock, sig_flags);

	add_timer(&timer);
	rtn = sock_recvmsg(sock, msg, size, flags);
	del_timer(&timer);

	spin_lock_irqsave(&current->sighand->siglock, sig_flags);
        flush_signals(current);
        current->blocked = blocked_save;
        recalc_sigpending();
        spin_unlock_irqrestore(&current->sighand->siglock, sig_flags);

	if(rtn < 0){
		return -ETIMEDOUT;  /* perhaps not the best error number */
	}
	return rtn;
}


/****************************************************************
*****************************************************************
**  Server code
**
**  The server code basically implements the dirty log ops.
**  However, they return an int to report any errors.  The
**  response to the client is loaded into either u.lr_int_rtn,
**  u.lr_region_rtn, or both; depending on the situation.
**
*****************************************************************
****************************************************************/

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
	printk("  %d\n", region - 1);
      } else if(range_count){
	printk("  %d - %d\n", region-range_count, region-1);
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
	  printk("  %d\n", region - 1);
	} else if(range_count){
	  printk("  %d - %d\n", region-range_count, region-1);
	}
	range_count = 0;
	region++;
      }
    }
  }

  if(range_count==1){
    printk("  %d\n", region - 1);
  } else if(range_count){
    printk("  %d - %d\n", region-range_count, region);
  }
  return count;
}

static int disk_resume(struct log_c *lc)
{
	int r;
	int good_count=0, bad_count=0;
	unsigned i;
	size_t size = lc->bitset_uint32_count * sizeof(uint32_t);
	struct region_user *tmp_ru, *ru;
	unsigned char live_nodes[16]; /* Attention -- max of 128 nodes... */

	printk("Disk Resume::\n");

	debug_disk_write = 1;

	memset(live_nodes, 0, sizeof(live_nodes));
	for(i = 0; i < global_count; i++){
		live_nodes[global_nodeids[i]/8] |= 1 << (global_nodeids[i]%8);
	}
	/* read the disk header */
	r = read_header(lc);
	if (r)
		return r;

	/* read the bits */
	r = read_bits(lc);
	if (r)
		return r;

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
			list_del(&ru->ru_list);
			kfree(ru);
		}
	}

	printk("  Live nodes        :: %d\n", global_count);
	printk("  In-Use Regions    :: %d\n", good_count+bad_count);
	printk("  Good IUR's        :: %d\n", good_count);
	printk("  Bad IUR's         :: %d\n", bad_count);

	lc->sync_count = count_bits32(lc->sync_bits, lc->bitset_uint32_count);

	printk("  Sync count        :: %Lu\n", lc->sync_count);
	printk("  Disk Region count :: %Lu\n", lc->header.nr_regions);
	printk("  Region count      :: %Lu\n", lc->region_count);

	if(lc->header.nr_regions != lc->region_count){
		printk("  NOTE:  Mapping has changed.\n");
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
					printk("Adding %u/%Lu\n",
					       new->ru_nodeid, new->ru_region);
					list_add(&new->ru_list, &lc->region_users);
				}
			}
		}
	}			

*/
	printk("Marked regions::\n");
	print_zero_bits((unsigned char *)lc->clean_bits, 0, lc->header.nr_regions);

	printk("Out-of-sync regions::\n");
	print_zero_bits((unsigned char *)lc->sync_bits, 0, lc->header.nr_regions);

	/* write the bits */
	r = write_bits(lc);
	if (r)
		return r;

	/* set the correct number of regions in the header */
	lc->header.nr_regions = lc->region_count;

	/* write the new header */
	return write_header(lc);
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


static int server_is_clean(struct log_c *lc, struct log_request *lr){
	lr->u.lr_int_rtn = log_test_bit(lc->clean_bits, lr->u.lr_region);
	return 0;
}


static int server_in_sync(struct log_c *lc, struct log_request *lr){
	lr->u.lr_int_rtn = log_test_bit(lc->sync_bits, lr->u.lr_region);
	return 0;
}


static int server_mark_region(struct log_c *lc, struct log_request *lr, uint32_t who){
	struct region_user *ru, *new;

#ifdef DEBUG
	if(who > 5 || lr->u.lr_region > lc->region_count){
		printk("Refusing to add crap to region_user list\n");
		printk("  who    :: %u\n", who);
		printk("  region :: %Lu\n", lr->u.lr_region);
		return -EINVAL;
	}
#endif

	new = kmalloc(sizeof(struct region_user), GFP_KERNEL);
	if(!new){
		return -ENOMEM;
	}

	new->ru_nodeid = who;
	new->ru_region = lr->u.lr_region;
    
	if(!(ru = find_ru_by_region(lc, lr->u.lr_region))){
		log_clear_bit(lc, lc->clean_bits, lr->u.lr_region);
		write_bits(lc);

		list_add(&new->ru_list, &lc->region_users);
	} else if(!find_ru(lc, who, lr->u.lr_region)){
		list_add(&new->ru_list, &ru->ru_list);
	} else {
		kfree(new);
	}

#ifdef DEBUG
	{int z=0;
	list_for_each_entry(ru, &lc->region_users, ru_list){
		z++;
		if(ru->ru_nodeid > 5){
			printk("\nNODEID (%u) IS TOO LARGE.\n", ru->ru_nodeid);
		}
		if(ru->ru_region > lc->region_count){
			printk("REGION (%Lu) IS OUT OF RANGE.\n", ru->ru_region);
		}
	}
	printk("MARK ::%d regions marked\n", z);
	}
#endif
	return 0;
}


static int server_clear_region(struct log_c *lc, struct log_request *lr, uint32_t who){
	struct region_user *ru;

	ru = find_ru(lc, who, lr->u.lr_region);
	if(!ru){
		/*
		if(!(log_test_bit(lc->recovering_bits, lr->u.lr_region))){
			DMWARN("request to remove unrecorded region user (%u/%Lu)",
			       who, lr->u.lr_region);
		}
		*/
		/*
		** ATTENTION -- may not be there because it is trying to clear **
		** a region it is resyncing, not because it marked it.  Need   **
		** more care ? */
		/*return -EINVAL;*/
	} else {
		list_del(&ru->ru_list);
		kfree(ru);
	}
#ifdef DEBUG
	{int z=0;
	list_for_each_entry(ru, &lc->region_users, ru_list){
		z++;
		if(ru->ru_nodeid > 5){
			printk("\nNODEID (%u) IS TOO LARGE.\n", ru->ru_nodeid);
		}
		if(ru->ru_region > lc->region_count){
			printk("REGION (%Lu) IS OUT OF RANGE.\n", ru->ru_region);
		}
	}
	if(!z) printk("CLEAR::%d regions marked\n", z);
	}
#endif
	if(!find_ru_by_region(lc, lr->u.lr_region)){
		log_set_bit(lc, lc->clean_bits, lr->u.lr_region);
		write_bits(lc);
	}
	return 0;
}


static int server_get_resync_work(struct log_c *lc, struct log_request *lr, uint32_t who)
{
	/* ATTENTION -- for now, if it's not me, do not assign work */
	if(my_id != who)
		return 0;

	lr->u.lr_int_rtn = _core_get_resync_work(lc, &(lr->u.lr_region_rtn));

	return 0;
}


static int server_complete_resync_work(struct log_c *lc, struct log_request *lr){
	int success = 1; /* ATTENTION -- need to get this from client */
	log_clear_bit(lc, lc->recovering_bits, lr->u.lr_region);
	if (success) {
		log_set_bit(lc, lc->sync_bits, lr->u.lr_region);
		lc->sync_count++;
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
				printk("Election processing failed.\n");
				return -1;
			}
			if(lc && (old != lc->server_id) && (my_id == lc->server_id)){
				printk("I'm cluster log server, READING DISK\n");
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

		suspend_on(&suspend_server_queue, atomic_read(&suspend_server));
		switch(restart_event_type){
		case SERVICE_NODE_LEAVE:
			/* ATTENTION -- may wish to check if regions **
			** are still in use by this node.  For now,  **
			** we do the same as if the node failed.  If **
			** there are no region still in-use by the   **
			** leaving node, it won't hurt anything - and**
			** if there is, they will be recovered.      */
		case SERVICE_NODE_FAILED:
			printk("A node has %s\n",
			       (restart_event_type == SERVICE_NODE_FAILED) ?
			       "failed." : "left the cluster.\n");
			
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
			printk("process_log_request:: failed\n");
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


static int start_server(void /* log_devices ? */){
	int error;

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


static void stop_server(void){
	atomic_set(&server_run, 0);

	wait_for_completion(&server_completion);
}


/****************************************************************
*****************************************************************
**  Client code
**
**  The client code is the implementation of the
**  "dirty_log_type" functions.  The client code
**  consults the server for information to fulfill the
**  various function calls.
*****************************************************************
****************************************************************/

static int run_election(struct log_c *lc){
	int error=0, len;
	struct sockaddr_in saddr_in;
	struct msghdr msg;
	struct iovec iov;
	mm_segment_t fs;
	struct log_request lr;  /* ATTENTION -- could be too much on the stack */
  
	memset(&lr, 0, sizeof(lr));

	printk("Running elections.\n");

	lr.lr_type = LRT_ELECTION;
	lr.u.lr_starter = my_id;
	lr.u.lr_coordinator = my_id;
	memcpy(lr.lr_uuid, lc->uuid, MAX_NAME_LEN);

	memset(&saddr_in, 0, sizeof(struct sockaddr_cl));

	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_iovlen = 1;
	msg.msg_iov = &iov;
	msg.msg_flags = 0;
  
	saddr_in.sin_family = AF_INET;
	saddr_in.sin_port = CLUSTER_LOG_PORT;
	saddr_in.sin_addr.s_addr = nodeid_to_ipaddr(my_id);
	msg.msg_name = &saddr_in;
	msg.msg_namelen = sizeof(saddr_in);

	iov.iov_len = sizeof(struct log_request);
	iov.iov_base = &lr;

	fs = get_fs();
	set_fs(get_ds());

	len = sock_sendmsg(lc->client_sock, &msg, sizeof(struct log_request));

	if(len < 0){
		printk("unable to send election notice to server (error = %d)\n", len);
		error = len;
		set_fs(fs);
		goto fail;
	}

  
	/* why do we need to reset this? */
	iov.iov_len = sizeof(struct log_request);
	iov.iov_base = &lr;

	len = my_recvmsg(lc->client_sock, &msg, sizeof(struct log_request),
			 0, 20);
	set_fs(fs);
  
	if(len > 0){
		lc->server_id = lr.u.lr_coordinator;
		printk("New cluster log server (nodeid = %u) designated.\n",
		       lc->server_id);
	} else {
		/* ATTENTION -- what do we do with this ? */
		DMWARN("Failed to recieve election results from server.");
		error = len;
	}

 fail:
	return error;
}

static int _consult_server(struct log_c *lc, region_t region,
			  int type, region_t *result, int *retry){
	int len;
	int error=0;
	struct sockaddr_in saddr_in;
	struct msghdr msg;
	struct iovec iov;
	mm_segment_t fs;
	struct log_request *lr;

	request_count++;

	lr = kmalloc(sizeof(struct log_request), GFP_KERNEL);
	if(!lr){
		error = -ENOMEM;
		*retry = 1;
		goto fail;
	}

	memset(lr, 0, sizeof(struct log_request));
	
	lr->lr_type = type;
	if(type == LRT_MASTER_LEAVING){
		lr->u.lr_starter = my_id;
	} else {
		lr->u.lr_region = region;
	}

	memcpy(lr->lr_uuid, lc->uuid, MAX_NAME_LEN);

	memset(&saddr_in, 0, sizeof(struct sockaddr_in));

	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_iovlen = 1;
	msg.msg_iov = &iov;
	msg.msg_flags = 0;
  
	saddr_in.sin_family = AF_INET;
	saddr_in.sin_port = CLUSTER_LOG_PORT;
	saddr_in.sin_addr.s_addr = nodeid_to_ipaddr(lc->server_id);
	msg.msg_name = &saddr_in;
	msg.msg_namelen = sizeof(saddr_in);

	iov.iov_len = sizeof(struct log_request);
	iov.iov_base = lr;
/*
	printk("To  :: 0x%x, %s\n", 
	       saddr_in.sin_addr.s_addr,
	       (lr->lr_type == LRT_IS_CLEAN)? "LRT_IS_CLEAN":
	       (lr->lr_type == LRT_IN_SYNC)? "LRT_IN_SYNC":
	       (lr->lr_type == LRT_MARK_REGION)? "LRT_MARK_REGION":
	       (lr->lr_type == LRT_GET_RESYNC_WORK)? "LRT_GET_RESYNC_WORK":
	       (lr->lr_type == LRT_GET_SYNC_COUNT)? "LRT_GET_SYNC_COUNT":
	       (lr->lr_type == LRT_CLEAR_REGION)? "LRT_CLEAR_REGION":
	       (lr->lr_type == LRT_COMPLETE_RESYNC_WORK)? "LRT_COMPLETE_RESYNC_WORK":
	       (lr->lr_type == LRT_MASTER_LEAVING)? "LRT_MASTER_LEAVING":
	       (lr->lr_type == LRT_ELECTION)? "LRT_ELECTION":
	       (lr->lr_type == LRT_SELECTION)? "LRT_SELECTION": "UNKNOWN"
		);
*/
	if(lr->lr_type == LRT_MARK_REGION){
		mark_req2ser++;
	}

	if(lr->lr_type == LRT_CLEAR_REGION){
		clear_req2ser++;
	}
	
	if(lr->lr_type == LRT_IN_SYNC){
		insync_req2ser++;
	}
	
	fs = get_fs();
	set_fs(get_ds());
  
	len = sock_sendmsg(lc->client_sock, &msg, sizeof(struct log_request));

	set_fs(fs);

	if(len < sizeof(struct log_request)){
		DMWARN("unable to send log request to server");
		error = -EBADE;
		goto fail;
	}

	iov.iov_len = sizeof(struct log_request);
	iov.iov_base = lr;

	fs = get_fs();
	set_fs(get_ds());

	if(type == LRT_MASTER_LEAVING){
		len = sock_recvmsg(lc->client_sock, &msg, sizeof(struct log_request),
				   /* WAIT for it */0);
	} else {
		len = my_recvmsg(lc->client_sock, &msg, sizeof(struct log_request),
				 0, 5);
	}
	set_fs(fs);

	if(len <= 0){
		/* ATTENTION -- what do we do with this ? */
//		DMWARN("Failed to recvmsg from cluster log server.");
		error = len;
		*retry = 1;
		goto fail;
	}
    
	if(lr->u.lr_int_rtn == -EAGAIN){
		DMWARN("server tells us to try again (%d).  Mirror suspended?",
		       lr->lr_type);
		*retry = 1;
		goto fail;
	}

	if(lr->u.lr_int_rtn == -ENXIO){
		DMWARN("server tells us it no longer controls the log.");
		lc->server_id = 0xDEAD;
		*retry = 1;
		goto fail;
	}

	if(lr->u.lr_int_rtn < 0){
		DMWARN("an error occured on the server while processing our request");
	}

	if(result)
		*result = lr->u.lr_region_rtn;

	error = lr->u.lr_int_rtn;
	kfree(lr);
	return error;
 fail:
	if(*retry){
		request_retry_count++;
		if(!(request_retry_count & 0x1F)){
			printk("Retried requests :: %u of %u (%u%%)\n",
			       request_retry_count,
			       request_count,
			       dm_div_up(request_retry_count*100,request_count));
		}
	}

	if(lr) kfree(lr);
	printk("_consult_server failed :: %d\n", error);
	return error;
}

static int consult_server(struct log_c *lc, region_t region,
			  int type, region_t *result){
	int rtn=0;
	int retry=0;
	int new_server=0;
	struct region_state *rs=NULL;

	/* ATTENTION -- need to change this, the server could fail at anypoint **
	** we do not want to send requests to the wrong place, or fail to run  **
	** an election when needed */

	do{
		retry = 0;
		suspend_on(&suspend_client_queue, atomic_read(&suspend_client));
		while(lc->server_id == 0xDEAD){
			run_election(lc);
			new_server = 1;
		}
			
		spin_lock(&region_state_lock);
		if(new_server){
			struct region_state *tmp_rs;
			printk("Our server died.\n");
			printk("Wiping clear region list.\n");
			list_for_each_entry_safe(rs, tmp_rs,
						 &clear_region_list, rs_list){
				list_del(&rs->rs_list);
				kfree(rs);
			}
			clear_region_count=0;
			printk("Resending all mark region requests.\n");
			list_for_each_entry(rs, &marked_region_list, rs_list){
				do {
					retry = 0;
					rtn = _consult_server(rs->rs_lc, rs->rs_region,
							      LRT_MARK_REGION, NULL, &retry);
				} while(retry);
			}
			if(type == LRT_MARK_REGION){
				/* we just handled all marks */
				spin_unlock(&region_state_lock);
				return rtn;
			}
		}

		if(!list_empty(&clear_region_list) && (clear_region_count > 100)){
			rs = list_entry(clear_region_list.next,
					struct region_state, rs_list);
			list_del(&rs->rs_list);
			clear_region_count--;
		}
		spin_unlock(&region_state_lock);
		
		/* ATTENTION -- it may be possible to remove a clear region **
		** request from the list.  Then, have a mark region happen  **
		** while we are here.  If the clear region request fails, it**
		** would be re-added - perhaps prematurely clearing the bit */
		
		if(rs){
			_consult_server(rs->rs_lc, rs->rs_region,
					LRT_CLEAR_REGION, NULL, &retry);

			if(retry){
				spin_lock(&region_state_lock);
				list_add(&rs->rs_list, &clear_region_list);
				clear_region_count++;
				spin_unlock(&region_state_lock);
			} else {
				kfree(rs);
			}
		}
		retry = 0;
		
		rtn = _consult_server(lc, region, type, result, &retry);
		schedule();
	} while(retry);

	return rtn;
}

/*----------------------------------------------------------------
 * cluster log constructor
 *
 * argv contains:
 *   [paranoid] <log_device | none> <region_size> [sync | nosync]
 *--------------------------------------------------------------*/
static int cluster_ctr(struct dirty_log *log, struct dm_target *ti,
		       unsigned int argc, char **argv)
{
	int error = 0;
	struct log_c *lc;
	struct sockaddr_in saddr_in;
	int paranoid;

	if(argc < 2){
		DMWARN("too few arguments to cluster mirror log");
		return -EINVAL;
	}

/*
	for(error = 0; error < argc; error++){
		printk("argv[%d] = %s\n", error, argv[error]);
	}
	error = 0;
*/
	paranoid = strcmp(argv[0], "paranoid") ? 0 : 1;

	if(!strcmp(argv[paranoid], "none")){
		/* ATTENTION -- set type to core */
		return -EINVAL;
		core_ctr(log, ti, (argc - 1)-paranoid, (argv + 1) + paranoid);
	} else {
		/* ATTENTION -- set type to disk */
		/* NOTE -- we take advantage of the fact that disk_ctr does **
		** not actually read the disk.  I suppose, however, that if **
		** it does in the future, we will simply reread it when a   **
		** servier is started here................................. */
		if((error = disk_ctr(log, ti, argc - paranoid, argv + paranoid))){
			DMWARN("disk_ctr failed.\n");
			return error;
		}
			
	}

	lc = log->context;

	memset(lc->uuid, 0, MAX_NAME_LEN);
	memcpy(lc->uuid, argv[paranoid],
	       (MAX_NAME_LEN < strlen(argv[paranoid])+1)? 
	       MAX_NAME_LEN-1:
	       strlen(argv[paranoid])); 

	lc->paranoid = paranoid;

	atomic_set(&lc->suspend, 1);
	printk("lc->in_sync :: UNSET\n");
	atomic_set(&lc->in_sync, 0);

	list_add(&lc->log_list, &log_list_head);
	INIT_LIST_HEAD(&lc->region_users);

	lc->server_id = 0xDEAD;

	error = sock_create(AF_INET, SOCK_DGRAM,
			    0,
			    &lc->client_sock);

	if(error){
		DMWARN("unable to create cluster log client socket");
		goto fail;
	}

	saddr_in.sin_family = AF_INET;
	saddr_in.sin_port = CLUSTER_LOG_PORT+1;
	saddr_in.sin_addr.s_addr = nodeid_to_ipaddr(my_id);
	error = lc->client_sock->ops->bind(lc->client_sock,
					   (struct sockaddr *)&saddr_in,
					   sizeof(struct sockaddr_in));
	while(error == -EADDRINUSE){
		saddr_in.sin_port++;
		error = lc->client_sock->ops->bind(lc->client_sock,
						   (struct sockaddr *)&saddr_in,
						   sizeof(struct sockaddr_in));
	}

	if(error){
		DMWARN("unable to bind cluster log client socket");
		sock_release(lc->client_sock);
		goto fail;
	}

	return 0;

 fail:
	/* ATTENTION -- if core is an option we must do appropriate */
	disk_dtr(log);
	return error;
}

static void cluster_dtr(struct dirty_log *log)
{
	struct log_c *lc = (struct log_c *) log->context;
	list_del(&lc->log_list);
	if(lc->server_id == my_id)
		consult_server(lc, 0, LRT_MASTER_LEAVING, NULL);
	sock_release(lc->client_sock);
	disk_dtr(log);
}


static int cluster_suspend(struct dirty_log *log){
	struct log_c *lc = (struct log_c *) log->context;
	atomic_set(&(lc->suspend), 1);
	return 0;
}

static int cluster_resume(struct dirty_log *log){
	struct log_c *lc = (struct log_c *) log->context;
	atomic_set(&(lc->suspend), 0);
	return 0;
}

static uint32_t cluster_get_region_size(struct dirty_log *log)
{
	struct log_c *lc = (struct log_c *) log->context;
	return lc->region_size;
}


static int cluster_is_clean(struct dirty_log *log, region_t region)
{
	int rtn;
	struct log_c *lc = (struct log_c *) log->context;
	rtn = consult_server(lc, region, LRT_IS_CLEAN, NULL);
	return rtn;
}

static int cluster_in_sync(struct dirty_log *log, region_t region, int block)
{
	int rtn;
	struct log_c *lc = (struct log_c *) log->context;
  
	/* check known_regions, return if found */

	insync_req++;

	if(atomic_read(&lc->in_sync)){
		return 1;
	}

	if(!block){
		return -EWOULDBLOCK;
	}

	rtn = consult_server(lc, region, LRT_IN_SYNC, NULL);
	return rtn;
}

static int cluster_flush(struct dirty_log *log)
{
	/* no need to flush, since server writes to disk before **
	** responding back to a client......................... */
	return 0;
}

static void cluster_mark_region(struct dirty_log *log, region_t region)
{
	int error = 0;
	struct region_state *rs, *tmp_rs, *rs_new;
	struct log_c *lc = (struct log_c *) log->context;

	mark_req++;

	rs_new = kmalloc(sizeof(struct region_state), GFP_ATOMIC);
	if(!rs_new){
		printk("Unable to allocate region_state for mark.\n");
		BUG();
	}

	spin_lock(&region_state_lock);
	list_for_each_entry_safe(rs, tmp_rs, &clear_region_list, rs_list){
		if(lc == rs->rs_lc && region == rs->rs_region){
			printk("Mark pre-empting clear of region %Lu\n", region);
			list_del(&rs->rs_list);
			list_add(&rs->rs_list, &marked_region_list);
			clear_region_count--;
			spin_unlock(&region_state_lock);
			kfree(rs_new);
			return;
		}
	}

	rs_new->rs_lc = lc;
	rs_new->rs_region = region;
	list_add(&rs->rs_list, &marked_region_list);

	spin_unlock(&region_state_lock);

	while((error = consult_server(lc, region, LRT_MARK_REGION, NULL))){
		DMWARN("unable to get server (%u) to mark region (%Lu)",
		       lc->server_id, region);
		DMWARN("Reason :: %d", error);
	}

	return;
}

static void cluster_clear_region(struct dirty_log *log, region_t region)
{
	struct log_c *lc = (struct log_c *) log->context;
	struct region_state *rs, *tmp_rs;

	clear_req++;

	spin_lock(&region_state_lock);

	list_for_each_entry_safe(rs, tmp_rs, &marked_region_list, rs_list){
		if(lc == rs->rs_lc && region == rs->rs_region){
			list_del(&rs->rs_list);
			list_add(&rs->rs_list, &clear_region_list);
			clear_region_count++;
			if(!(clear_region_count & 0x7F)){
				printk("clear_region_count :: %d\n", clear_region_count);
			}
			spin_unlock(&region_state_lock);
			return;
		}
	}

	spin_unlock(&region_state_lock);
	printk("HEY!  Clearing region that is not marked.\n");

	return;
}

static int cluster_get_resync_work(struct dirty_log *log, region_t *region)
{
	int rtn;
	struct log_c *lc = (struct log_c *) log->context;

	rtn = consult_server(lc, 0, LRT_GET_RESYNC_WORK, region);

	return rtn;
}

static void cluster_complete_resync_work(struct dirty_log *log,
					 region_t region, int success)
{
	struct log_c *lc = (struct log_c *) log->context;

	while(consult_server(lc, region, LRT_COMPLETE_RESYNC_WORK, NULL)){
		DMWARN("unable to notify server of completed resync work");
	}

	return;
}

static region_t cluster_get_sync_count(struct dirty_log *log)
{
	region_t rtn;
	struct log_c *lc = (struct log_c *) log->context;
	if(consult_server(lc, 0, LRT_GET_SYNC_COUNT, &rtn)){
		return 0;
	}

	if(rtn > lc->region_count){
		DMERR("sync_count (%Lu) > region_count (%Lu) - this can not be!",
		      rtn, lc->region_count);
	}

	if(rtn >= lc->region_count){
		if(!(atomic_read(&lc->in_sync))){
			printk("lc->in_sync :: SET\n");
		}
		atomic_set(&lc->in_sync, 1);
	}

	return rtn;
}

static int cluster_status(struct dirty_log *log, status_type_t status,
			  char *result, unsigned int maxlen)
{
	int sz = 0;
	char buffer[16];
	struct log_c *lc = (struct log_c *) log->context;
	struct region_user *ru;
	int i=0;

	switch(status){
	case STATUSTYPE_INFO:
		spin_lock(&region_state_lock);
		i = clear_region_count;
		spin_unlock(&region_state_lock);
		printk("CLIENT OUTPUT::\n");
		printk("  Server           : %u\n", lc->server_id);
		printk("  In-sync          : %s\n", (atomic_read(&lc->in_sync))?
		       "YES" : "NO");
		printk("  Regions clearing : %d\n", i);
		printk("  Mark requests    : %d\n", mark_req);
		printk("  Mark req to serv : %d (%d%%)\n", mark_req2ser,
		       (mark_req2ser*100)/mark_req);
		printk("  Clear requests   : %d\n", clear_req);
		printk("  Clear req to serv: %d (%d%%)\n", clear_req2ser,
		       (clear_req2ser*100)/clear_req);
		printk("  Sync  requests   : %d\n", insync_req);
		printk("  Sync req to serv : %d (%d%%)\n", insync_req2ser,
		       (insync_req2ser*100)/insync_req);
		if(lc->server_id == my_id){
			atomic_set(&suspend_server, 1);
			printk("SERVER OUTPUT::\n");
			printk("Marked regions::\n");
			print_zero_bits((unsigned char *)lc->clean_bits, 0,
					lc->header.nr_regions);

			printk("Out-of-sync regions::\n");
			print_zero_bits((unsigned char *)lc->sync_bits, 0,
					lc->header.nr_regions);

			printk("Region user list::\n");
			list_for_each_entry(ru, &lc->region_users, ru_list){
				printk("  %u, %Lu\n", ru->ru_nodeid, ru->ru_region);
			}
			atomic_set(&suspend_server, 0);
			wake_up_all(&suspend_server_queue);
		}
                break;

        case STATUSTYPE_TABLE:
		format_dev_t(buffer, lc->log_dev->bdev->bd_dev);
                DMEMIT("%s %u %s " SECTOR_FORMAT " ", log->type->name,
                       lc->sync == DEFAULTSYNC ? 2 : 3, buffer, lc->region_size);
		if (lc->sync != DEFAULTSYNC)
			DMEMIT("%ssync ", lc->sync == NOSYNC ? "no" : "");
        }
	

	return sz;
}


/****************************************************************
*****************************************************************
**  Service manager tie-in functions
**
**
*****************************************************************
****************************************************************/
static int clog_stop(void *data){
	struct log_c *lc;

	printk("Cluster stop received.\n");

	printk("Suspending client operations.\n");
	atomic_set(&suspend_client, 1);

	list_for_each_entry(lc, &log_list_head, log_list){
		printk("lc->in_sync :: UNSET\n");
		atomic_set(&lc->in_sync, 0);
	}
	
	printk("Suspending cluster log server.\n");
	atomic_set(&suspend_server, 1);

	return 0;
}

static int clog_start(void *data, uint32_t *nodeids, int count, int event_id, int type){
	int i;
	uint32_t server;
	struct log_c *lc;
	struct kcl_cluster_node node;

	if(global_nodeids){
		kfree(global_nodeids);
	}
	global_nodeids = nodeids;
	global_count = count;

	kcl_get_node_by_nodeid(0, &node);
	my_id = node.node_id;

	restart_event_id = event_id;
	restart_event_type = type;

	switch(type){
	case SERVICE_NODE_LEAVE:
	case SERVICE_NODE_FAILED:
		list_for_each_entry(lc, &log_list_head, log_list){
			for(i=0, server = 0xDEAD; i < count; i++){
				if(lc->server_id == nodeids[i]){
					server = nodeids[i];
				}
			}
			/* ATTENTION -- need locking around this ? */
			lc->server_id = server;
		}
		break;
	case SERVICE_NODE_JOIN:
		break;
	default:
		DMERR("Invalid service event type received.");
		BUG();
		break;
	}
	printk("Resuming cluster log server.\n");
	atomic_set(&suspend_server, 0);
	wake_up_all(&suspend_server_queue);
	return 0;
}

static void clog_finish(void *data, int event_id){
	DMINFO("Cluster event processed.  Restarting operations.");

	printk("Resuming client operations.\n\n");
	
	atomic_set(&suspend_client, 0);
	wake_up_all(&suspend_client_queue);
}


static struct kcl_service_ops clog_ops = {
	.stop = clog_stop,
	.start = clog_start,
	.finish = clog_finish,
};

static struct dirty_log_type _cluster_type = {
	.name = "cluster",
	.module = THIS_MODULE,
	.ctr = cluster_ctr,
	.dtr = cluster_dtr,
	.suspend = cluster_suspend,
	.resume = cluster_resume,
	.get_region_size = cluster_get_region_size,
	.is_clean = cluster_is_clean,
	.in_sync = cluster_in_sync,
	.flush = cluster_flush,
	.mark_region = cluster_mark_region,
	.clear_region = cluster_clear_region,
	.get_resync_work = cluster_get_resync_work,
	.complete_resync_work = cluster_complete_resync_work,
	.get_sync_count = cluster_get_sync_count,
	.status = cluster_status,
};


static int __init cluster_dirty_log_init(void)
{
	int r;

	INIT_LIST_HEAD(&clear_region_list);
	INIT_LIST_HEAD(&marked_region_list);
	spin_lock_init(&region_state_lock);
	init_waitqueue_head(&suspend_client_queue);
	init_waitqueue_head(&suspend_server_queue);

	r = kcl_register_service("cluster_log", 11, SERVICE_LEVEL_GDLM, &clog_ops,
				 1, NULL, &local_id);
	if(r){
		DMWARN("couldn't register service");
		goto fail1;
	}

	r = start_server();
	if(r){
		DMWARN("unable to start cluster log server daemon");
		goto fail2;
	}

	r = kcl_join_service(local_id);

	if(r){
		DMWARN("couldn't join service group");
		goto fail3;
	}

	r = dm_register_dirty_log_type(&_cluster_type);
	if (r) {
		DMWARN("couldn't register cluster dirty log type");
		goto fail4;
	}

	return 0;

 fail4:
	kcl_leave_service(local_id);
 fail3:
	stop_server();
 fail2:
	kcl_unregister_service(local_id);
 fail1:
	return r;

}

static void __exit cluster_dirty_log_exit(void)
{
	if(!list_empty(&log_list_head)){
		DMERR("attempt to remove module, but dirty logs are still in place!");
		DMERR("this is a fatal error");
		BUG();
	}
	dm_unregister_dirty_log_type(&_cluster_type);
	kcl_leave_service(local_id);
	stop_server();
	kcl_unregister_service(local_id);
}

module_init(cluster_dirty_log_init);
module_exit(cluster_dirty_log_exit);

MODULE_DESCRIPTION(DM_NAME " cluster capable mirror logs");
MODULE_AUTHOR("Jonathan Brassow");
MODULE_LICENSE("GPL");

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
