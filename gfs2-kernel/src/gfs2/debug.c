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

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <asm/semaphore.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>

#define WANT_DEBUG_NAMES
#include "gfs2.h"

#undef kmalloc
#undef kfree

void * gmalloc_nofail_real(unsigned int size, int flags, char *file,
			   unsigned int line)
{
	void *x;
	for (;;) {
		x = kmalloc(size, flags);
		if (x)
			return x;
		if (time_after_eq(jiffies, gfs2_malloc_warning + 5 * HZ)) {
			printk("GFS2: out of memory: %s, %u\n", __FILE__, __LINE__);
			gfs2_malloc_warning = jiffies;
		}
		yield();
	}
}

#if defined(GFS2_MEMORY_SIMPLE)

atomic_t gfs2_memory_count;

void gfs2_memory_add_i(void *data, char *file, unsigned int line)
{
	atomic_inc(&gfs2_memory_count);
}

void gfs2_memory_rm_i(void *data, char *file, unsigned int line)
{
	if (data)
		atomic_dec(&gfs2_memory_count);
}

void *gmalloc(unsigned int size, int flags, char *file, unsigned int line)
{
	void *data = kmalloc(size, flags);
	if (data)
		atomic_inc(&gfs2_memory_count);
	return data;
}

void *gmalloc_nofail(unsigned int size, int flags, char *file,
		     unsigned int line)
{
	atomic_inc(&gfs2_memory_count);
	return gmalloc_nofail_real(size, flags, file, line);
}

void gfree(void *data, char *file, unsigned int line)
{
	if (data) {
		atomic_dec(&gfs2_memory_count);
		kfree(data);
	}
}

void gfs2_memory_init(void)
{
	atomic_set(&gfs2_memory_count, 0);
}

void gfs2_memory_uninit(void)
{
	int x = atomic_read(&gfs2_memory_count);
	if (x)
		printk("GFS2: %d memory allocations remaining\n", x);
}

#elif defined(GFS2_MEMORY_BRUTE)

#define GFS2_MEMORY_HASH_SHIFT       13
#define GFS2_MEMORY_HASH_SIZE        (1 << GFS2_MEMORY_HASH_SHIFT)
#define GFS2_MEMORY_HASH_MASK        (GFS2_MEMORY_HASH_SIZE - 1)

struct gfs2_memory {
	struct list_head gm_list;
	void *gm_data;
	char *gm_file;
	unsigned int gm_line;
};

static spinlock_t memory_lock;
static struct list_head memory_list[GFS2_MEMORY_HASH_SIZE];

static __inline__ struct list_head *memory_bucket(void *data)
{
	return memory_list +
		(gfs2_hash(&data, sizeof(void *)) &
		 GFS2_MEMORY_HASH_MASK);
}

void gfs2_memory_add_i(void *data, char *file, unsigned int line)
{
	struct gfs2_memory *gm;

	RETRY_MALLOC(gm = kmalloc(sizeof(struct gfs2_memory), GFP_KERNEL), gm);
	gm->gm_data = data;
	gm->gm_file = file;
	gm->gm_line = line;

	spin_lock(&memory_lock);
	list_add(&gm->gm_list, memory_bucket(data));
	spin_unlock(&memory_lock);
}

void gfs2_memory_rm_i(void *data, char *file, unsigned int line)
{
	struct list_head *head = memory_bucket(data);
	struct list_head *tmp;
	struct gfs2_memory *gm = NULL;

	if (!data)
		return;

	spin_lock(&memory_lock);
	for (tmp = head->next; tmp != head; tmp = tmp->next) {
		gm = list_entry(tmp, struct gfs2_memory, gm_list);
		if (gm->gm_data == data) {
			list_del(tmp);
			break;
		}
	}
	spin_unlock(&memory_lock);

	if (tmp == head)
		printk("GFS2: freeing unalloced memory from (%s, %u)\n",
		       file, line);
	else
		kfree(gm);
}

void *gmalloc(unsigned int size, int flags, char *file, unsigned int line)
{
	void *data = kmalloc(size, flags);
	if (data)
		gfs2_memory_add_i(data, file, line);
	return data;
}

void *gmalloc_nofail(unsigned int size, int flags, char *file,
		     unsigned int line)
{
	void *data = gmalloc_nofail_real(size, flags, file, line);
	gfs2_memory_add_i(data, file, line);
	return data;
}

void gfree(void *data, char *file, unsigned int line)
{
	if (data) {
		gfs2_memory_rm_i(data, file, line);
		kfree(data);
	}
}

void gfs2_memory_init(void)
{
	unsigned int x;

	spin_lock_init(&memory_lock);
	for (x = 0; x < GFS2_MEMORY_HASH_SIZE; x++)
		INIT_LIST_HEAD(memory_list + x);
}

void gfs2_memory_uninit(void)
{
	unsigned int x;
	struct list_head *head;
	struct gfs2_memory *gm;

	for (x = 0; x < GFS2_MEMORY_HASH_SIZE; x++) {
		head = memory_list + x;
		while (!list_empty(head)) {
			gm = list_entry(head->next, struct gfs2_memory, gm_list);
			printk("GFS2: unfreed memory from (%s, %u)\n",
			       gm->gm_file, gm->gm_line);
			list_del(&gm->gm_list);
			kfree(gm);
		}
	}
}

#endif

