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

/* memory.c
 * 
 * memory allocation routines
 * 
 */

#include "dlm_internal.h"
#include "memory.h"
#include "config.h"

/* as the man says...Shouldn't this be in a header file somewhere? */
#define	BYTES_PER_WORD		sizeof(void *)

static kmem_cache_t *rsb_cache_small;
static kmem_cache_t *rsb_cache_large;
static kmem_cache_t *lkb_cache;
static kmem_cache_t *lvb_cache;
static kmem_cache_t *resdir_cache_large;
static kmem_cache_t *resdir_cache_small;

/* The thresholds above which we allocate large RSBs/resdatas rather than small 
 * ones. This must make the resultant structure end on a word boundary */
#define LARGE_RSB_NAME 28
#define LARGE_RES_NAME 28

int dlm_memory_init()
{
	int ret = -ENOMEM;


	rsb_cache_small =
	    kmem_cache_create("dlm_rsb(small)",
			      (sizeof(struct dlm_rsb) + LARGE_RSB_NAME + BYTES_PER_WORD-1) & ~(BYTES_PER_WORD-1),
			      __alignof__(struct dlm_rsb), 0, NULL, NULL);
	if (!rsb_cache_small)
		goto out;

	rsb_cache_large =
	    kmem_cache_create("dlm_rsb(large)",
			      sizeof(struct dlm_rsb) + DLM_RESNAME_MAXLEN,
			      __alignof__(struct dlm_rsb), 0, NULL, NULL);
	if (!rsb_cache_large)
		goto out_free_rsbs;

	lkb_cache = kmem_cache_create("dlm_lkb", sizeof(struct dlm_lkb),
				      __alignof__(struct dlm_lkb), 0, NULL, NULL);
	if (!lkb_cache)
		goto out_free_rsbl;

	resdir_cache_large =
	    kmem_cache_create("dlm_resdir(l)",
			      sizeof(struct dlm_direntry) + DLM_RESNAME_MAXLEN,
			      __alignof__(struct dlm_direntry), 0, NULL, NULL);
	if (!resdir_cache_large)
		goto out_free_lkb;

	resdir_cache_small =
	    kmem_cache_create("dlm_resdir(s)",
			      (sizeof(struct dlm_direntry) + LARGE_RES_NAME + BYTES_PER_WORD-1) & ~(BYTES_PER_WORD-1),
			      __alignof__(struct dlm_direntry), 0, NULL, NULL);
	if (!resdir_cache_small)
		goto out_free_resl;

	/* LVB cache also holds ranges, so should be 64bit aligned */
	lvb_cache = kmem_cache_create("dlm_lvb/range", DLM_LVB_LEN,
				      __alignof__(uint64_t), 0, NULL, NULL);
	if (!lkb_cache)
		goto out_free_ress;

	ret = 0;
	goto out;

      out_free_ress:
	kmem_cache_destroy(resdir_cache_small);

      out_free_resl:
	kmem_cache_destroy(resdir_cache_large);

      out_free_lkb:
	kmem_cache_destroy(lkb_cache);

      out_free_rsbl:
	kmem_cache_destroy(rsb_cache_large);

      out_free_rsbs:
	kmem_cache_destroy(rsb_cache_small);

      out:
	return ret;
}

void dlm_memory_exit()
{
	kmem_cache_destroy(rsb_cache_large);
	kmem_cache_destroy(rsb_cache_small);
	kmem_cache_destroy(lkb_cache);
	kmem_cache_destroy(resdir_cache_small);
	kmem_cache_destroy(resdir_cache_large);
	kmem_cache_destroy(lvb_cache);
}

struct dlm_rsb *allocate_rsb(struct dlm_ls *ls, int namelen)
{
	struct dlm_rsb *r;

	DLM_ASSERT(namelen <= DLM_RESNAME_MAXLEN,);

	if (namelen >= LARGE_RSB_NAME)
		r = kmem_cache_alloc(rsb_cache_large, ls->ls_allocation);
	else
		r = kmem_cache_alloc(rsb_cache_small, ls->ls_allocation);

	if (r)
		memset(r, 0, sizeof(struct dlm_rsb) + namelen);

	return r;
}

void free_rsb(struct dlm_rsb *r)
{
	int length = r->res_length;

#ifdef POISON
	memset(r, 0x55, sizeof(struct dlm_rsb) + r->res_length);
#endif

	if (length >= LARGE_RSB_NAME)
		kmem_cache_free(rsb_cache_large, r);
	else
		kmem_cache_free(rsb_cache_small, r);
}

struct dlm_lkb *allocate_lkb(struct dlm_ls *ls)
{
	struct dlm_lkb *l;

	l = kmem_cache_alloc(lkb_cache, ls->ls_allocation);
	if (l)
		memset(l, 0, sizeof(struct dlm_lkb));

	return l;
}

void free_lkb(struct dlm_lkb *l)
{
#ifdef POISON
	memset(l, 0xAA, sizeof(struct dlm_lkb));
#endif
	kmem_cache_free(lkb_cache, l);
}

struct dlm_direntry *allocate_resdata(struct dlm_ls *ls, int namelen)
{
	struct dlm_direntry *rd;

	DLM_ASSERT(namelen <= DLM_RESNAME_MAXLEN,);

	if (namelen >= LARGE_RES_NAME)
		rd = kmem_cache_alloc(resdir_cache_large, ls->ls_allocation);
	else
		rd = kmem_cache_alloc(resdir_cache_small, ls->ls_allocation);

	if (rd)
		memset(rd, 0, sizeof(struct dlm_direntry));

	return rd;
}

void free_resdata(struct dlm_direntry *de)
{
	if (de->length >= LARGE_RES_NAME)
		kmem_cache_free(resdir_cache_large, de);
	else
		kmem_cache_free(resdir_cache_small, de);
}

char *allocate_lvb(struct dlm_ls *ls)
{
	char *l;

	l = kmem_cache_alloc(lvb_cache, ls->ls_allocation);
	if (l)
		memset(l, 0, DLM_LVB_LEN);

	return l;
}

void free_lvb(char *l)
{
	kmem_cache_free(lvb_cache, l);
}

/* Ranges are allocated from the LVB cache as they are the same size (4x64
 * bits) */
uint64_t *allocate_range(struct dlm_ls * ls)
{
	uint64_t *l;

	l = kmem_cache_alloc(lvb_cache, ls->ls_allocation);
	if (l)
		memset(l, 0, DLM_LVB_LEN);

	return l;
}

void free_range(uint64_t *l)
{
	kmem_cache_free(lvb_cache, l);
}

struct dlm_rcom *allocate_rcom_buffer(struct dlm_ls *ls)
{
	struct dlm_rcom *rc;

	rc = kmalloc(dlm_config.buffer_size, ls->ls_allocation);
	if (rc)
		memset(rc, 0, dlm_config.buffer_size);

	return rc;
}

void free_rcom_buffer(struct dlm_rcom *rc)
{
	kfree(rc);
}
