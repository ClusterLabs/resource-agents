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

#include "dlm_internal.h"
#include "config.h"

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

	lkb = kmalloc(sizeof(*lkb), GFP_KERNEL);
	if (lkb)
		memset(lkb, 0, sizeof(*lkb));

	return lkb;
}

void free_lkb(struct dlm_lkb *lkb)
{
	kfree(lkb);
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

	l = kmalloc(DLM_LVB_LEN, GFP_KERNEL);
	if (l)
		memset(l, 0, DLM_LVB_LEN);

	return l;
}

void free_lvb(char *l)
{
	kfree(l);
}

uint64_t *allocate_range(struct dlm_ls *ls)
{
	uint64_t *p;
	int len = sizeof(uint64_t) * 4;

	p = kmalloc(len, GFP_KERNEL);
	if (p)
		memset(p, 0, len);

	return p;
}

void free_range(uint64_t *l)
{
	kfree(l);
}

struct dlm_rcom *allocate_rcom_buffer(struct dlm_ls *ls)
{
	struct dlm_rcom *rc;

	rc = kmalloc(dlm_config.buffer_size, GFP_KERNEL);
	if (rc)
		memset(rc, 0, dlm_config.buffer_size);

	return rc;
}

void free_rcom_buffer(struct dlm_rcom *rc)
{
	kfree(rc);
}
