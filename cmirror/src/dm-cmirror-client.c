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
#include <cluster/service.h>
#include <cluster/cnxman.h>
#include <cluster/cnxman-socket.h>

#include "dm-log.h"
#include "dm-io.h"
#include "dm-cmirror-xfr.h"
#include "dm-cmirror-common.h"
#include "dm-cmirror-server.h"
#include "dm-cmirror-cman.h"

LIST_HEAD(log_list_head);

struct region_state {
	struct log_c *rs_lc;
	region_t rs_region;
	struct list_head rs_list;
};

static mempool_t *region_state_pool = NULL;
static spinlock_t region_state_lock;
static int clear_region_count=0;
static struct list_head clear_region_list;
static struct list_head marked_region_list;

static int shutting_down=0;
static atomic_t suspend_client;
static wait_queue_head_t suspend_client_queue;


/* These vars are just for stats, and will be removed */
static uint32_t request_count=0;
static uint32_t request_retry_count=0;
static int clear_req=0;
static int mark_req=0;
static int insync_req=0;
static int clear_req2ser=0;
static int mark_req2ser=0;
static int insync_req2ser=0;


static void *region_state_alloc(int gfp_mask, void *pool_data){
	return kmalloc(sizeof(struct region_state), gfp_mask);
}

static void region_state_free(void *element, void *pool_data){
	kfree(element);
}

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

	region_count = dm_sector_div_up(ti->len, region_size);

	lc = kmalloc(sizeof(*lc), GFP_KERNEL);
	if (!lc) {
		DMWARN("Couldn't allocate core log");
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
		DMWARN("Unable to get device %s", argv[0]);
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



static int run_election(struct log_c *lc){
	int error=0, len;
	struct sockaddr_in saddr_in;
	struct msghdr msg;
	struct iovec iov;
	mm_segment_t fs;
	struct log_request lr;  /* ATTENTION -- could be too much on the stack */
  
	memset(&lr, 0, sizeof(lr));

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
	if(!(saddr_in.sin_addr.s_addr = nodeid_to_ipaddr(my_id))){
		DMERR("Unable to convert nodeid_to_ipaddr in run_election");
	}
	msg.msg_name = &saddr_in;
	msg.msg_namelen = sizeof(saddr_in);

	iov.iov_len = sizeof(struct log_request);
	iov.iov_base = &lr;

	fs = get_fs();
	set_fs(get_ds());

	len = sock_sendmsg(lc->client_sock, &msg, sizeof(struct log_request));

	if(len < 0){
		DMERR("unable to send election notice to server (error = %d)", len);
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
		DMINFO("New cluster log server (nodeid = %u) designated",
		       lc->server_id);
	} else {
		/* ATTENTION -- what do we do with this ? */
		DMWARN("Failed to recieve election results from server");
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
	if(!(saddr_in.sin_addr.s_addr = nodeid_to_ipaddr(lc->server_id))){
		DMERR("Unable to convert nodeid_to_ipaddr in _consult_server");
		error = -ENXIO;
		*retry = 1;
		goto fail;
	}
	msg.msg_name = &saddr_in;
	msg.msg_namelen = sizeof(saddr_in);

	iov.iov_len = sizeof(struct log_request);
	iov.iov_base = lr;
/*
	DMERR("To  :: 0x%x, %s", 
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
//		DMWARN("Failed to recvmsg from cluster log server");
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
		DMWARN("server tells us it no longer controls the log");
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
			DMINFO("Cluster mirror retried requests :: %u of %u (%u%%)",
			       request_retry_count,
			       request_count,
			       dm_div_up(request_retry_count*100, request_count));
		}
	}

	if(lr) kfree(lr);
#ifdef DEBUG
	DMWARN("Request (%s) to server failed :: %d",
	       (type == LRT_IS_CLEAN)? "LRT_IS_CLEAN":
	       (type == LRT_IN_SYNC)? "LRT_IN_SYNC":
	       (type == LRT_MARK_REGION)? "LRT_MARK_REGION":
	       (type == LRT_GET_RESYNC_WORK)? "LRT_GET_RESYNC_WORK":
	       (type == LRT_GET_SYNC_COUNT)? "LRT_GET_SYNC_COUNT":
	       (type == LRT_CLEAR_REGION)? "LRT_CLEAR_REGION":
	       (type == LRT_COMPLETE_RESYNC_WORK)? "LRT_COMPLETE_RESYNC_WORK":
	       (type == LRT_MASTER_LEAVING)? "LRT_MASTER_LEAVING":
	       (type == LRT_ELECTION)? "LRT_ELECTION":
	       (type == LRT_SELECTION)? "LRT_SELECTION": "UNKNOWN",
	       error);
#endif
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
		if(new_server && 
		   (!list_empty(&clear_region_list) ||
		    !list_empty(&marked_region_list))){
			int i=0;
			struct region_state *tmp_rs;

			DMWARN("Clean-up required due to server failure");
			DMWARN(" - Wiping clear region list");
			list_for_each_entry_safe(rs, tmp_rs,
						 &clear_region_list, rs_list){
				i++;
				list_del_init(&rs->rs_list);
				mempool_free(rs, region_state_pool);
			}
			clear_region_count=0;
			DMWARN(" - %d clear region requests wiped", i);

			DMWARN(" - Resending all mark region requests");
			list_for_each_entry(rs, &marked_region_list, rs_list){
				do {
					retry = 0;
					DMWARN("   - " SECTOR_FORMAT, rs->rs_region);
					rtn = _consult_server(rs->rs_lc, rs->rs_region,
							      LRT_MARK_REGION, NULL, &retry);
				} while(retry);
			}
			DMWARN("Clean-up complete");
			if(type == LRT_MARK_REGION){
				/* we just handled all marks */
				DMWARN("Mark request ignored.\n");
				spin_unlock(&region_state_lock);

				return rtn;
			} else {
				DMWARN("Continuing request:: %s", 
				      (type == LRT_IS_CLEAN)? "LRT_IS_C	LEAN":
				      (type == LRT_IN_SYNC)? "LRT_IN_SYNC":
				      (type == LRT_MARK_REGION)? "LRT_MARK_REGION":
				      (type == LRT_GET_RESYNC_WORK)? "LRT_GET_RESYNC_WORK":
				      (type == LRT_GET_SYNC_COUNT)? "LRT_GET_SYNC_COUNT":
				      (type == LRT_CLEAR_REGION)? "LRT_CLEAR_REGION":
				      (type == LRT_COMPLETE_RESYNC_WORK)? "LRT_COMPLETE_RESYNC_WORK":
				      (type == LRT_MASTER_LEAVING)? "LRT_MASTER_LEAVING":
				      (type == LRT_ELECTION)? "LRT_ELECTION":
				      (type == LRT_SELECTION)? "LRT_SELECTION": "UNKNOWN"
					);
			}
		}

		rs = NULL;

		if(!list_empty(&clear_region_list)){
			rs = list_entry(clear_region_list.next,
					struct region_state, rs_list);
			list_del_init(&rs->rs_list);
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
				mempool_free(rs, region_state_pool);
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
		DMERR("Too few arguments to cluster mirror");
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
		** server is started here.................................. */
		if((error = disk_ctr(log, ti, argc - paranoid, argv + paranoid))){
			DMWARN("Cluster mirror:: disk_ctr failed");
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
	atomic_set(&lc->in_sync, -1);

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
	if(!(saddr_in.sin_addr.s_addr = nodeid_to_ipaddr(my_id))){
		DMERR("Unable to convert nodeid_to_ipaddr in cluster_ctr");
	}
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
	list_del_init(&lc->log_list);
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

	if(atomic_read(&lc->in_sync) == 1){
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
	struct log_c *lc = (struct log_c *) log->context;
	if(lc->paranoid){
		/* there should be no pending requests */
		return 0;
	} else {
		/* flush all clear_region requests to server */
		/* ATTENTION -- not implemented */
		return 0;
	}
}

static void cluster_mark_region(struct dirty_log *log, region_t region)
{
	int error = 0;
	struct region_state *rs, *tmp_rs, *rs_new;
	struct log_c *lc = (struct log_c *) log->context;

	mark_req++;

	spin_lock(&region_state_lock);
	list_for_each_entry_safe(rs, tmp_rs, &clear_region_list, rs_list){
		if(lc == rs->rs_lc && region == rs->rs_region){
#ifdef DEBUG
			DMINFO("Mark pre-empting clear of region %Lu", region);
#endif
			list_del_init(&rs->rs_list);
			list_add(&rs->rs_list, &marked_region_list);
			clear_region_count--;
			spin_unlock(&region_state_lock);

			return;
		}
	}
	/* ATTENTION -- this check should not be necessary.   **
	** Why are regions being marked again before a clear? */
	list_for_each_entry(rs, &marked_region_list, rs_list){
		if(lc == rs->rs_lc && region == rs->rs_region){
#ifdef DEBUG
			DMINFO("Double mark on region ("
			       SECTOR_FORMAT ")", region);
#endif
			spin_unlock(&region_state_lock);

			return;
		}
	}

	/* ATTENTION -- Do I want to alloc outside of the spinlock? */
	rs_new = mempool_alloc(region_state_pool, GFP_KERNEL);
	if(!rs_new){
		DMERR("Unable to allocate region_state for mark.");
		BUG();
	}

	rs_new->rs_lc = lc;
	rs_new->rs_region = region;
	INIT_LIST_HEAD(&rs_new->rs_list);
	list_add(&rs_new->rs_list, &marked_region_list);

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
	struct region_state *rs, *tmp_rs, *rs_new;

	clear_req++;

	spin_lock(&region_state_lock);

	list_for_each_entry_safe(rs, tmp_rs, &clear_region_list, rs_list){
		if(lc == rs->rs_lc && region == rs->rs_region){
			DMINFO("%d) Double clear on region ("
			      SECTOR_FORMAT ")", __LINE__, region);
			spin_unlock(&region_state_lock);
			return;
		}
	}

	list_for_each_entry_safe(rs, tmp_rs, &marked_region_list, rs_list){
		if(lc == rs->rs_lc && region == rs->rs_region){
			list_del_init(&rs->rs_list);
			list_add(&rs->rs_list, &clear_region_list);
			clear_region_count++;
			if(!(clear_region_count & 0x7F)){
				DMINFO("clear_region_count :: %d", clear_region_count);
			}
			spin_unlock(&region_state_lock);
			return;
		}
	}

	/* We can get here because we my be doing resync_work, and therefore, **
	** clearing without ever marking..................................... */

	/* ATTENTION -- Do I want to alloc outside of the spinlock? */
	rs_new = mempool_alloc(region_state_pool, GFP_ATOMIC);
	if(!rs_new){
		DMERR("Unable to allocate region_state for mark.");
		BUG();
	}

	rs_new->rs_lc = lc;
	rs_new->rs_region = region;
	INIT_LIST_HEAD(&rs_new->rs_list);
	list_add(&rs_new->rs_list, &clear_region_list);
	clear_region_count++;
	if(!(clear_region_count & 0x7F)){
		DMINFO("clear_region_count :: %d", clear_region_count);
	}

	spin_unlock(&region_state_lock);
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
	if(atomic_read(&lc->in_sync)){
		return lc->region_count;
	}

	if(consult_server(lc, 0, LRT_GET_SYNC_COUNT, &rtn)){
		return 0;
	}

	if(rtn > lc->region_count){
		DMERR("sync_count (%Lu) > region_count (%Lu) - this can not be!",
		      rtn, lc->region_count);
	}

	if(rtn >= lc->region_count){
		if(unlikely(atomic_read(&lc->in_sync) < 0)){
			DMINFO("Initial mirror status:: IN SYNC");
		}
		if(!atomic_read(&lc->in_sync)){
			DMINFO("Mirror status:: IN SYNC");
		}
		atomic_set(&lc->in_sync, 1);
	} else if(unlikely(atomic_read(&lc->in_sync) < 0)){
		DMINFO("Initial mirror status:: NOT IN SYNC");
		atomic_set(&lc->in_sync, 0);
	}

	return rtn;
}

static int cluster_status(struct dirty_log *log, status_type_t status,
			  char *result, unsigned int maxlen)
{
	int sz = 0;
	char buffer[16];
	struct log_c *lc = (struct log_c *) log->context;
	struct region_state *rs;
	int i=0, j=0;

	switch(status){
	case STATUSTYPE_INFO:
		spin_lock(&region_state_lock);
		i = clear_region_count;
		list_for_each_entry(rs, &marked_region_list, rs_list){
			j++;
		}
		spin_unlock(&region_state_lock);

		DMINFO("CLIENT OUTPUT::");
		DMINFO("  My ID            : %u", my_id);
		DMINFO("  Server ID        : %u", lc->server_id);
		DMINFO("  In-sync          : %s", (atomic_read(&lc->in_sync)>0)?
		       "YES" : "NO");
		DMINFO("  Regions marked   : %d", j);
		DMINFO("  Regions clearing : %d", i);
		DMINFO("  Mark requests    : %d", mark_req);
		DMINFO("  Mark req to serv : %d (%d%%)", mark_req2ser,
		       (mark_req2ser*100)/(mark_req+1));
		DMINFO("  Clear requests   : %d", clear_req);
		DMINFO("  Clear req to serv: %d (%d%%)", clear_req2ser,
		       (clear_req2ser*100)/(clear_req+1));
		DMINFO("  Sync  requests   : %d", insync_req);
		DMINFO("  Sync req to serv : %d (%d%%)", insync_req2ser,
		       (insync_req2ser*100)/(insync_req+1));
		if(lc->server_id == my_id){
			print_server_status(lc);
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

static int clog_stop(void *data){
	struct log_c *lc;

	DMINFO("Cluster mirror 'stop' initiated");

	DMINFO(" - Suspending client operations");
	atomic_set(&suspend_client, 1);

	DMINFO(" - Clearing mirror sync status");
	list_for_each_entry(lc, &log_list_head, log_list){
		DMINFO(" - Mirror status changed for <name>:: NOT IN SYNC");
		atomic_set(&lc->in_sync, 0);
	}
	
	if(likely(!shutting_down)){
		DMINFO(" - Suspending cluster log server");
		suspend_server();
	} else {
		DMINFO(" - Cluster mirror shutting down");
	}

	DMINFO("Cluster mirror 'stop' complete");
	return 0;
}

static int clog_start(void *data, uint32_t *nodeids, int count, int event_id, int type){
	int i;
	uint32_t server;
	struct log_c *lc;
	struct kcl_cluster_node node;

	DMINFO("Cluster mirror 'start' initiated");
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
		DMERR("Invalid service event type received");
		BUG();
		break;
	}
	DMINFO(" - Resuming cluster log server");
	resume_server();
	DMINFO(" - Resuming cluster log server: done");
	DMINFO("Cluster mirror 'start' complete");
	return 0;
}

static void clog_finish(void *data, int event_id){
	DMINFO("Cluster mirror 'finish' initiated");

	DMINFO(" - Resuming client operations");
	atomic_set(&suspend_client, 0);
	wake_up_all(&suspend_client_queue);
	DMINFO(" - Resuming client operations: done");
	DMINFO("Cluster mirror 'finish' complete");
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
	int r=0;

	INIT_LIST_HEAD(&clear_region_list);
	INIT_LIST_HEAD(&marked_region_list);

	spin_lock_init(&region_state_lock);
	region_state_pool = mempool_create(20, region_state_alloc,
					   region_state_free, NULL);
	if(!region_state_pool){
		DMWARN("couldn't create region state pool");
		goto fail1;
	}

	init_waitqueue_head(&suspend_client_queue);


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

	/* By setting 'shutting_down', the server will not be suspended **
	** when a stop is recieved */
	shutting_down = 1;
	kcl_leave_service(local_id);
	stop_server();
	kcl_unregister_service(local_id);
}

module_init(cluster_dirty_log_init);
module_exit(cluster_dirty_log_exit);

MODULE_DESCRIPTION(DM_NAME " cluster capable mirror logs (cluster mirroring)");
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
