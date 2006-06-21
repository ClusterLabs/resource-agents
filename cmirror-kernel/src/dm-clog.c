/*
 * Copyright (C) 2006 Red Hat, Inc.
 *
 * This file is released under the LGPL.
 */

#include "dm-clog-tfr.h"

struct flush_entry {
	int type;
	region_t region;
	struct list_head list;
};

struct log_c {
	struct dm_target *ti;
	uint32_t region_size;
	region_t region_count;
	int failure_response;
	char uuid[MAX_NAME_LEN];

	spinlock_t flush_lock;
	struct list_head flush_list;  /* only for clear and mark requests */
};

static mempool_t *flush_entry_pool = NULL;

static void *flush_entry_alloc(int gfp_mask, void *pool_data)
{
	return kmalloc(sizeof(struct flush_entry), gfp_mask);
}

static void flush_entry_free(void *element, void *pool_data)
{
	kfree(element);
}

static int cluster_ctr(struct dirty_log *log, struct dm_target *ti,
		       unsigned int argc, char **argv, int disk_log)
{
	int i;
	int r = 0;
	int failure_response = FR_NONBLOCK;
	struct log_c *lc = NULL;
	uint32_t region_size;
	region_t region_count;

	/* Already checked argument count */

	/* Check for block_on_error.  It must be present. */
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "block_on_error"))
			failure_response = FR_BLOCK;
	}
	if (failure_response != FR_BLOCK) {
		DMWARN("Required \"block_on_error\" argument not supplied.");
		return -EINVAL;
	}

	if (sscanf(argv[0], SECTOR_FORMAT, &region_size) != 1) {
		DMWARN("Invalid region size string");
		return -EINVAL;
	}

	region_count = dm_sector_div_up(ti->len, region_size);

	lc = kmalloc(sizeof(*lc), GFP_KERNEL);
	if (!lc) {
		DMWARN("Unable to allocate cluster log context.");
		return -ENOMEM;
	}
	lc->ti = ti;
	lc->region_size = region_size;
	lc->region_count = region_count;

	/* FIXME: Send table string to server */

fail:
	if (lc)
		kfree(lc);
	
	return -ENOSYS;
}

/*
 * cluster_core_ctr
 * @log
 * @ti
 * @argc
 * @argv
 *
 * argv contains:
 *   <region_size> <uuid> [[no]sync] "block_on_error"
 *
 * Returns: 0 on success, -XXX on failure
 */
static int cluster_core_ctr(struct dirty_log *log, struct dm_target *ti,
			    unsigned int argc, char **argv)
{
	int i;
	if ((argc < 3) || (argc > 4)) {
		DMERR("Too %s arguments to clustered_core mirror log type.",
		      (argc < 3) ? "few" : "many");
		DMERR("  %d arguments supplied:", argc);
		for (i = 0; i < argc; i++)
			DMERR("    %s", argv[i]);
		return -EINVAL;
	}

	return cluster_ctr(log, ti, argc, argv, 0);
}


/*
 * cluster_core_ctr
 * @log
 * @ti
 * @argc
 * @argv
 *
 * argv contains:
 *   <disk> <region_size> <uuid> [[no]sync] "block_on_error"
 *--------------------------------------------------------------*/
static int cluster_disk_ctr(struct dirty_log *log, struct dm_target *ti,
			    unsigned int argc, char **argv)
{
	int i;
	if ((argc < 4) || (argc > 5)) {
		DMERR("Too %s arguments to clustered_disk mirror log type.",
		      (argc < 4) ? "few" : "many");
		DMERR("  %d arguments supplied:", argc);
		for (i = 0; i < argc; i++)
			DMERR("    %s", argv[i]);
		return -EINVAL;
	}

	return cluster_ctr(log, ti, argc, argv, 1);
}

static void cluster_dtr(struct dirty_log *log)
{
	struct log_c *lc = (struct log_c *)log->context;

	/* FIXME: Send shutdown to server */
	kfree(lc);

	return;
}

static int cluster_presuspend(struct dirty_log *log)
{
	return -ENOSYS;
}

static int cluster_postsuspend(struct dirty_log *log)
{
	return -ENOSYS;
}

static int cluster_resume(struct dirty_log *log)
{
	return -ENOSYS;
}

/*
 * cluster_get_region_size
 * @log
 *
 * Only called during mirror construction, ok to block.
 *
 * Returns: region size (doesn't fail)
 */
static uint32_t cluster_get_region_size(struct dirty_log *log)
{
	struct log_c *lc = (struct log_c *)log->context;

	return lc->region_size;
}

static int cluster_is_clean(struct dirty_log *log, region_t region)
{
	return 0; /* not clean for now */
}

static int cluster_is_remote_recovering(struct dirty_log *log, region_t region)
{
	return 1; /* yes for now */
}

/*
 * cluster_in_sync
 * @log
 * @region
 * @can_block: if set, return immediately
 *
 * Returns: 1 if in-sync, 0 if not-in-sync, < 0 on error
 */
static int cluster_in_sync(struct dirty_log *log, region_t region, int can_block)
{
	if (!can_block)
		return -EWOULDBLOCK;

	return 0; /* not in sync for now */
}

/*
 * cluster_flush
 * @log
 *
 * This function is ok to block.
 * The flush happens in two stages.  First, it sends all
 * clear/mark requests that are on the list.  Then it
 * tells the server to commit them.  This gives the
 * server a chance to optimise the commit to the cluster
 * and/or disk, instead of doing it for every request.
 *
 * Additionally, we could implement another thread that
 * sends the requests up to the server - reducing the
 * load on flush.  Then the flush would have less in
 * the list and be responsible for the finishing commit.
 *
 * Returns: 0 on success, < 0 on failure
 */
static int cluster_flush(struct dirty_log *log)
{
	int r = 0;
	int flags;
	region_t region;
	struct log_c *lc = (struct log_c *)log->context;
	struct list_head flush_list;
	struct flush_entry *fe, *tmp_fe;

	spin_lock_irqsave(&lc->flush_lock, flags);
	flush_list = lc->flush_list;
	spin_unlock_irqrestore(&lc->flush_lock, flags);

	/*
	 * FIXME: Count up requests, group request types,
	 * allocate memory to stick all requests in and
	 * send to server in one go.  Failing the allocation,
	 * do it one by one.
	 */

	list_for_each_entry(fe, &flush_list, list) {
		r = dm_clog_consult_server(lc->uuid, fe->type,
					   (char *)&fe->region,
					   sizeof(fe->region),
					   NULL, 0);
		if (r) {
			r = (r > 0) ? -r : r;
			goto fail;
		}
	}

	r = dm_clog_consult_server(lc->uuid, DM_CLOG_FLUSH,
				   NULL, 0, NULL, 0);
	if (r)
		r = (r > 0) ? -r : r;

fail:
	list_for_each_entry_safe(fe, tmp_fe, &flush_list, list) {
		list_del(&fe->list);
		mempool_free(fe, flush_entry_pool);
	}

	r = -EIO;

	return r;
}

/*
 * cluster_mark_region
 * @log
 * @region
 *
 * This function should avoid blocking unless absolutely required.
 * (Memory allocation is valid for blocking.)
 */
static void cluster_mark_region(struct dirty_log *log, region_t region)
{
	int flags;
	struct log_c *lc = (struct log_c *)log->context;
	struct flush_entry *fe;

	/* Wait for an allocation, but _never_ fail */
	fe = mempool_alloc(flush_enrty_pool, GFP_KERNEL);
	BUG_ON(!fe);

	spin_lock_irqsave(&lc->flush_lock, flags);
	fe->type = DM_CLOG_MARK_REGION;
	fe->region = region;
	list_add(&fe->list, &lc->flush_list);
	spin_unlock_irqrestore(&lc->flush_lock, flags);
		
	return;
}

/*
 * cluster_clear_region
 * @log
 * @region
 *
 * This function must not block.
 * So, the alloc can't block.  In the worst case, it is ok to
 * fail.  It would simply mean we can't clear the region.
 * Does nothing to current sync context, but does mean
 * the region will be re-sync'ed on a reload of the mirror
 * even though it is in-sync.
 */
static void cluster_clear_region(struct dirty_log *log, region_t region)
{
	int flags;
	struct log_c *lc = (struct log_c *)log->context;
	struct flush_entry *fe;

	fe = mempool_alloc(flush_enrty_pool, GFP_ATOMIC);
	if (!fe) {
		DMERR("Failed to allocate memory to clear region.");
		return;
	}
	spin_lock_irqsave(&lc->flush_lock, flags);
	fe->type = DM_CLOG_CLEAR_REGION;
	fe->region = region;
	list_add(&fe->list, &lc->flush_list);
	spin_unlock_irqrestore(&lc->flush_lock, flags);
	
	return;
}

static int cluster_get_resync_work(struct dirty_log *log, region_t *region)
{
	return -ENOSYS;
}

static void cluster_set_region_sync(struct dirty_log *log,
				    region_t region, int in_sync)
{
	return;
}

static region_t cluster_get_sync_count(struct dirty_log *log)
{
	return 0;
}

static int cluster_status(struct dirty_log *log, status_type_t status_type,
			  char *result, unsigned int maxlen)
{
	return -ENOSYS;
}

status int cluster_get_failure_response(struct dirty_log *log)
{
	struct log_c *lc = (struct log_c *)log->context;

	return lc->failure_response;
}

static struct dirty_log_type _clustered_core_type = {
	.name = "clustered_core",
	.module = THIS_MODULE,
	.ctr = cluster_core_ctr,
	.dtr = cluster_dtr,
	.presuspend = cluster_presuspend,
	.postsuspend = cluster_postsuspend,
	.resume = cluster_resume,
	.get_region_size = cluster_get_region_size,
	.is_clean = cluster_is_clean,
	.is_remote_recovering = cluster_is_remote_recovering,
	.in_sync = cluster_in_sync,
	.flush = cluster_flush,
	.mark_region = cluster_mark_region,
	.clear_region = cluster_clear_region,
	.get_resync_work = cluster_get_resync_work,
	.set_region_sync = cluster_set_region_sync,
	.get_sync_count = cluster_get_sync_count,
	.status = cluster_status,
	.get_failure_response = cluster_get_failure_response,
};

static struct dirty_log_type _clustered_disk_type = {
	.name = "clustered_disk",
	.module = THIS_MODULE,
	.ctr = cluster_disk_ctr,
	.dtr = cluster_dtr,
	.presuspend = cluster_presuspend,
	.postsuspend = cluster_postsuspend,
	.resume = cluster_resume,
	.get_region_size = cluster_get_region_size,
	.is_clean = cluster_is_clean,
	.is_remote_recovering = cluster_is_remote_recovering,
	.in_sync = cluster_in_sync,
	.flush = cluster_flush,
	.mark_region = cluster_mark_region,
	.clear_region = cluster_clear_region,
	.get_resync_work = cluster_get_resync_work,
	.set_region_sync = cluster_set_region_sync,
	.get_sync_count = cluster_get_sync_count,
	.status = cluster_status,
	.get_failure_response = cluster_get_failure_response,
};

static int __init cluster_dirty_log_init(void)
{
	int r = 0;

	flush_entry_pool = mempool_create(100, flush_entry_alloc,
					  flush_entry_free, NULL);

	if (!flush_entry_pool) {
		DMERR("Unable to create flush_entry_pool:  No memory.");
		return -ENOMEM;
	}

	r = dm_register_dirty_log_type(&_clustered_core_type);
	if (r) {
		DMWARN("Couldn't register clustered_core dirty log type");
		return r;
	}

	r = dm_register_dirty_log_type(&_clustered_disk_type);
	if (r) {
		DMWARN("Couldn't register clustered_disk dirty log type");
		dm_unregister_dirty_log_type(&_clustered_core_type);
		return r;
	}

	return r;
}

static void __exit cluster_dirty_log_exit(void)
{
	dm_unregister_dirty_log_type(&_clustered_disk_type);
	dm_unregister_dirty_log_type(&_clustered_core_type);
	return;
}
