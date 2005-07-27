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

#ifndef __DEBUG_DOT_H__
#define __DEBUG_DOT_H__

#if defined(GFS2_MEMORY_SIMPLE) || defined(GFS2_MEMORY_BRUTE)

void gfs2_memory_init(void);
void gfs2_memory_uninit(void);
void gfs2_memory_add_i(void *data, char *file, unsigned int line);
void gfs2_memory_rm_i(void *data, char *file, unsigned int line);
void *gmalloc(unsigned int size, int flags, char *file, unsigned int line);
void gfree(void *data, char *file, unsigned int line);

#define gfs2_memory_add(data)	gfs2_memory_add_i((data), __FILE__, __LINE__)
#define gfs2_memory_rm(data)	gfs2_memory_rm_i((data), __FILE__, __LINE__)
#define kmalloc(size, flags)	gmalloc((size), (flags), __FILE__, __LINE__)
#define kfree(data)		gfree((data), __FILE__, __LINE__)

void *gmalloc_nofail(unsigned int size, int flags, char *file,
		     unsigned int line);
#define kmalloc_nofail(size, flags) \
	gmalloc_nofail((size), (flags), __FILE__, __LINE__)

#else
#define gfs2_memory_init()	do {} while (0)
#define gfs2_memory_uninit()	do {} while (0)
#define gfs2_memory_add(x)	do {} while (0)
#define gfs2_memory_rm(x)	do {} while (0)

void *gmalloc_nofail_real(unsigned int size, int flags, char *file,
			  unsigned int line);
#define kmalloc_nofail(size, flags) \
	gmalloc_nofail_real((size), (flags), __FILE__, __LINE__)
#endif

#endif /* __DEBUG_DOT_H__ */

