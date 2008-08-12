#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>

#include "gfs.h"

#define pv(struct, member, fmt) printk("  "#member" = "fmt"\n", struct->member);

#define WANT_GFS_CONVERSION_FUNCTIONS
#include "gfs_ondisk.h"

