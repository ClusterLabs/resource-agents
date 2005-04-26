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
#include "ops_export.h"

/**
 * gfs2_decode_fh -
 * @param1: description
 * @param2: description
 * @param3: description
 *
 * Function description
 *
 * Returns: what is returned
 */

static struct dentry *
gfs2_decode_fh(struct super_block *sb, __u32 *fh, int fh_len, int fh_type,
	      int (*acceptable)(void *context, struct dentry *dentry),
	      void *context)
{
	ENTER(G2FN_DECODE_FH)
	RETURN(G2FN_DECODE_FH, ERR_PTR(-ENOSYS));
}

/**
 * gfs2_encode_fh -
 * @param1: description
 * @param2: description
 * @param3: description
 *
 * Function description
 *
 * Returns: what is returned
 */

static int
gfs2_encode_fh(struct dentry *dentry, __u32 *fh, int *len,
	      int connectable)
{
	ENTER(G2FN_ENCODE_FH)
	RETURN(G2FN_ENCODE_FH, 255);
}

/**
 * gfs2_get_name -
 * @param1: description
 * @param2: description
 * @param3: description
 *
 * Function description
 *
 * Returns: what is returned
 */

static int
gfs2_get_name(struct dentry *parent, char *name,
	     struct dentry *child)
{
	ENTER(G2FN_GET_NAME)
	RETURN(G2FN_GET_NAME, -ENOSYS);
}

/**
 * gfs2_get_parent -
 * @param1: description
 * @param2: description
 * @param3: description
 *
 * Function description
 *
 * Returns: what is returned
 */

static struct dentry *
gfs2_get_parent(struct dentry *child)
{
	ENTER(G2FN_GET_PARENT)
	RETURN(G2FN_GET_PARENT, ERR_PTR(-ENOSYS));
}

/**
 * gfs2_get_dentry -
 * @param1: description
 * @param2: description
 * @param3: description
 *
 * Function description
 *
 * Returns: what is returned
 */

static struct dentry *
gfs2_get_dentry(struct super_block *sb, void *inump)
{
	ENTER(G2FN_GET_DENTRY)
	RETURN(G2FN_GET_DENTRY, ERR_PTR(-ENOSYS));
}

struct export_operations gfs2_export_ops = {
	.decode_fh = gfs2_decode_fh,
	.encode_fh = gfs2_encode_fh,
	.get_name = gfs2_get_name,
	.get_parent = gfs2_get_parent,
	.get_dentry = gfs2_get_dentry,
};

