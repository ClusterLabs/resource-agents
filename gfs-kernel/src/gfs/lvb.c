#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>

#include "gfs.h"

#define pv(struct, member, fmt) printk("  "#member" = "fmt"\n", struct->member);

#define CPIN_08(s1, s2, member, count) {memcpy((s1->member), (s2->member), (count));}
#define CPOUT_08(s1, s2, member, count) {memcpy((s2->member), (s1->member), (count));}
#define CPIN_16(s1, s2, member) {(s1->member) = gfs16_to_cpu((s2->member));}
#define CPOUT_16(s1, s2, member) {(s2->member) = cpu_to_gfs16((s1->member));}
#define CPIN_32(s1, s2, member) {(s1->member) = gfs32_to_cpu((s2->member));}
#define CPOUT_32(s1, s2, member) {(s2->member) = cpu_to_gfs32((s1->member));}
#define CPIN_64(s1, s2, member) {(s1->member) = gfs64_to_cpu((s2->member));}
#define CPOUT_64(s1, s2, member) {(s2->member) = cpu_to_gfs64((s1->member));}

/**
 * gfs_rgrp_lvb_in - Read in rgrp data
 * @rb: the cpu-order structure
 * @lvb: the lvb
 *
 */

void
gfs_rgrp_lvb_in(struct gfs_rgrp_lvb *rb, char *lvb)
{
	struct gfs_rgrp_lvb *str = (struct gfs_rgrp_lvb *)lvb;

	CPIN_32(rb, str, rb_magic);
	CPIN_32(rb, str, rb_free);
	CPIN_32(rb, str, rb_useddi);
	CPIN_32(rb, str, rb_freedi);
	CPIN_32(rb, str, rb_usedmeta);
	CPIN_32(rb, str, rb_freemeta);
}

/**
 * gfs_rgrp_lvb_out - Write out rgrp data
 * @rb: the cpu-order structure
 * @lvb: the lvb
 *
 */

void
gfs_rgrp_lvb_out(struct gfs_rgrp_lvb *rb, char *lvb)
{
	struct gfs_rgrp_lvb *str = (struct gfs_rgrp_lvb *)lvb;

	CPOUT_32(rb, str, rb_magic);
	CPOUT_32(rb, str, rb_free);
	CPOUT_32(rb, str, rb_useddi);
	CPOUT_32(rb, str, rb_freedi);
	CPOUT_32(rb, str, rb_usedmeta);
	CPOUT_32(rb, str, rb_freemeta);
}

/**
 * gfs_rgrp_lvb_print - Print out rgrp data
 * @rb: the cpu-order structure
 * @console - TRUE if this should be printed to the console,
 *            FALSE if it should be just printed to the incore debug
 *            buffer
 */

void
gfs_rgrp_lvb_print(struct gfs_rgrp_lvb *rb)
{
	pv(rb, rb_magic, "%u");
	pv(rb, rb_free, "%u");
	pv(rb, rb_useddi, "%u");
	pv(rb, rb_freedi, "%u");
	pv(rb, rb_usedmeta, "%u");
	pv(rb, rb_freemeta, "%u");
}

/**
 * gfs_quota_lvb_in - Read in quota data
 * @rb: the cpu-order structure
 * @lvb: the lvb
 *
 */

void
gfs_quota_lvb_in(struct gfs_quota_lvb *qb, char *lvb)
{
	struct gfs_quota_lvb *str = (struct gfs_quota_lvb *)lvb;

	CPIN_32(qb, str, qb_magic);
	CPIN_32(qb, str, qb_pad);
	CPIN_64(qb, str, qb_limit);
	CPIN_64(qb, str, qb_warn);
	CPIN_64(qb, str, qb_value);
}

/**
 * gfs_quota_lvb_out - Write out quota data
 * @rb: the cpu-order structure
 * @lvb: the lvb
 *
 */

void
gfs_quota_lvb_out(struct gfs_quota_lvb *qb, char *lvb)
{
	struct gfs_quota_lvb *str = (struct gfs_quota_lvb *)lvb;

	CPOUT_32(qb, str, qb_magic);
	CPOUT_32(qb, str, qb_pad);
	CPOUT_64(qb, str, qb_limit);
	CPOUT_64(qb, str, qb_warn);
	CPOUT_64(qb, str, qb_value);
}

/**
 * gfs_quota_lvb_print - Print out quota data
 * @rb: the cpu-order structure
 * @console - TRUE if this should be printed to the console,
 *            FALSE if it should be just printed to the incore debug
 *            buffer
 */

void
gfs_quota_lvb_print(struct gfs_quota_lvb *qb)
{
	pv(qb, qb_magic, "%u");
	pv(qb, qb_pad, "%u");
	pv(qb, qb_limit, "%"PRIu64);
	pv(qb, qb_warn, "%"PRIu64);
	pv(qb, qb_value, "%"PRId64);
}
