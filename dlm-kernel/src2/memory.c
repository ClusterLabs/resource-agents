/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**  
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "dlm_internal.h"
#include "config.h"

static kmem_cache_t *lkb_cache;
static kmem_cache_t *lvb_cache;


int dlm_memory_init(void)
{
	int ret = -ENOMEM;

	lkb_cache = kmem_cache_create("dlm_lkb", sizeof(struct dlm_lkb),
				__alignof__(struct dlm_lkb), 0, NULL, NULL);
	if (!lkb_cache)
		goto out;

	lvb_cache = kmem_cache_create("dlm_lvb", DLM_LVB_LEN,
				__alignof__(uint64_t), 0, NULL, NULL);

	if (!lvb_cache)
		goto out_lkb;

	return 0;

 out_lkb:
	kmem_cache_destroy(lkb_cache);
 out:
	return ret;
}

void dlm_memory_exit(void)
{
	kmem_cache_destroy(lkb_cache);
	kmem_cache_destroy(lvb_cache);
}

/* FIXME: have some minimal space built-in to rsb for the name and
   kmalloc a separate name if needed, like dentries are done */

struct dlm_rsb *allocate_rsb(struct dlm_ls *ls, int namelen)
{
	struct dlm_rsb *r;

	DLM_ASSERT(namelen <= DLM_RESNAME_MAXLEN,);

	r = kmalloc(sizeof(*r) + namelen, GFP_KERNEL);
	if (r)
		memset(r, 0, sizeof(*r) + namelen);
	return r;
}

void free_rsb(struct dlm_rsb *r)
{
	kfree(r);
}

struct dlm_lkb *allocate_lkb(struct dlm_ls *ls)
{
	struct dlm_lkb *lkb;

	lkb = kmem_cache_alloc(lkb_cache, GFP_KERNEL);
	if (lkb)
		memset(lkb, 0, sizeof(*lkb));
	return lkb;
}

void free_lkb(struct dlm_lkb *lkb)
{
	kmem_cache_free(lkb_cache, lkb);
}

struct dlm_direntry *allocate_direntry(struct dlm_ls *ls, int namelen)
{
	struct dlm_direntry *de;

	DLM_ASSERT(namelen <= DLM_RESNAME_MAXLEN,);

	de = kmalloc(sizeof(*de) + namelen, GFP_KERNEL);
	if (de)
		memset(de, 0, sizeof(*de) + namelen);
	return de;
}

void free_direntry(struct dlm_direntry *de)
{
	kfree(de);
}

char *allocate_lvb(struct dlm_ls *ls)
{
	char *l;

	l = kmem_cache_alloc(lvb_cache, GFP_KERNEL);
	if (l)
		memset(l, 0, DLM_LVB_LEN);
	return l;
}

void free_lvb(char *l)
{
	kmem_cache_free(lvb_cache, l);
}

/* use lvb cache since they are the same size */

uint64_t *allocate_range(struct dlm_ls *ls)
{
	uint64_t *p;

	p = kmem_cache_alloc(lvb_cache, GFP_KERNEL);
	if (p)
		memset(p, 0, 4*sizeof(uint64_t));
	return p;
}

void free_range(uint64_t *l)
{
	kmem_cache_free(lvb_cache, l);
}
