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

#include "gfs2.h"

#define pv(struct, member, fmt) printk("  "#member" = "fmt"\n", struct->member);

#define CPIN_08(s1, s2, member, count) {memcpy((s1->member), (s2->member), (count));}
#define CPOUT_08(s1, s2, member, count) {memcpy((s2->member), (s1->member), (count));}
#define CPIN_16(s1, s2, member) {(s1->member) = gfs2_16_to_cpu((s2->member));}
#define CPOUT_16(s1, s2, member) {(s2->member) = cpu_to_gfs2_16((s1->member));}
#define CPIN_32(s1, s2, member) {(s1->member) = gfs2_32_to_cpu((s2->member));}
#define CPOUT_32(s1, s2, member) {(s2->member) = cpu_to_gfs2_32((s1->member));}
#define CPIN_64(s1, s2, member) {(s1->member) = gfs2_64_to_cpu((s2->member));}
#define CPOUT_64(s1, s2, member) {(s2->member) = cpu_to_gfs2_64((s1->member));}

/**
 * gfs2_quota_lvb_in - Read in quota data
 * @rb: the cpu-order structure
 * @lvb: the lvb
 *
 */

void
gfs2_quota_lvb_in(struct gfs2_quota_lvb *qb, char *lvb)
{
	ENTER(G2FN_QUOTA_LVB_IN)
	struct gfs2_quota_lvb *str = (struct gfs2_quota_lvb *)lvb;

	CPIN_32(qb, str, qb_magic);
	CPIN_32(qb, str, qb_pad);
	CPIN_64(qb, str, qb_limit);
	CPIN_64(qb, str, qb_warn);
	CPIN_64(qb, str, qb_value);

	RET(G2FN_QUOTA_LVB_IN);
}

/**
 * gfs2_quota_lvb_out - Write out quota data
 * @rb: the cpu-order structure
 * @lvb: the lvb
 *
 */

void
gfs2_quota_lvb_out(struct gfs2_quota_lvb *qb, char *lvb)
{
	ENTER(G2FN_QUOTA_LVB_OUT)
	struct gfs2_quota_lvb *str = (struct gfs2_quota_lvb *)lvb;

	CPOUT_32(qb, str, qb_magic);
	CPOUT_32(qb, str, qb_pad);
	CPOUT_64(qb, str, qb_limit);
	CPOUT_64(qb, str, qb_warn);
	CPOUT_64(qb, str, qb_value);

	RET(G2FN_QUOTA_LVB_OUT);
}

/**
 * gfs2_quota_lvb_print - Print out quota data
 * @rb: the cpu-order structure
 * @console - TRUE if this should be printed to the console,
 *            FALSE if it should be just printed to the incore debug
 *            buffer
 */

void
gfs2_quota_lvb_print(struct gfs2_quota_lvb *qb)
{
	ENTER(G2FN_QUOTA_LVB_PRINT)

	pv(qb, qb_magic, "%u");
	pv(qb, qb_pad, "%u");
	pv(qb, qb_limit, "%"PRIu64);
	pv(qb, qb_warn, "%"PRIu64);
	pv(qb, qb_value, "%"PRId64);

	RET(G2FN_QUOTA_LVB_PRINT);
}
