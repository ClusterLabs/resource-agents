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


#ifdef GFS2_TRACE
int gfs2_trace_enter(unsigned int flag);
void gfs2_trace_exit(unsigned int flag, int dummy);
#define ENTER_TRACE(x) int gfs2_trace_var = gfs2_trace_enter((x));
#define EXIT_TRACE(x) gfs2_trace_exit((x), gfs2_trace_var);
int gfs2_trace_init(void);
void gfs2_trace_uninit(void);

#else
#define ENTER_TRACE(x)
#define EXIT_TRACE(x)
#define gfs2_trace_init() (0)
#define gfs2_trace_uninit()
#endif


#ifdef GFS2_PROFILE
extern void *gfs2_profile_cookie;
#define ENTER_PROFILE(x) uint64_t gfs2_profile_var = kdbl_profile_enter();
#define EXIT_PROFILE(x) kdbl_profile_exit(gfs2_profile_cookie, (x), gfs2_profile_var);
int gfs2_profile_init(void);
void gfs2_profile_uninit(void);

#else
#define ENTER_PROFILE(x)
#define EXIT_PROFILE(x)
#define gfs2_profile_init() (0)
#define gfs2_profile_uninit()
#endif


#if defined(GFS2_TRACE) || defined(GFS2_PROFILE)
#include <linux/kdbl.h>
#include <linux/gfs2_debug_const.h>
#define ENTER(x) ENTER_PROFILE(x) ENTER_TRACE(x)
#define ENTER2(x) ENTER(x)
#define EXIT(x) EXIT_TRACE(x) EXIT_PROFILE(x)
#define RET(x) \
do { \
	EXIT_TRACE(x) EXIT_PROFILE(x) \
	return; \
} while (0)
#define RETURN(x, y) \
do { \
	typeof(y) RETURN_var = y; \
	EXIT_TRACE(x) EXIT_PROFILE(x) \
	return RETURN_var; \
} while (0)
#define DO_PROF(x, y) \
do { \
	ENTER_PROFILE(x) ENTER_TRACE(x) \
	{y;} \
	EXIT_TRACE(x) EXIT_PROFILE(x) \
} while (0)
#define DO_PROF2(x, y) DO_PROF(x, y)

#else
#define ENTER(x)
#define ENTER2(x)
#define EXIT(x)
#define RET(x) return
#define RETURN(x, y) return y
#define DO_PROF(x, y) {y;}
#define DO_PROF2(x, y) DO_PROF(x, y)
#endif
#define DBFLAG(x)


#if defined(GFS2_MEMORY_SIMPLE) || defined(GFS2_MEMORY_BRUTE)

void gfs2_memory_add_i(void *data, char *file, unsigned int line);
void gfs2_memory_rm_i(void *data, char *file, unsigned int line);
void *gmalloc(unsigned int size, int flags,
	      char *file, unsigned int line);
void *gmalloc_nofail(unsigned int size, int flags,
		     char *file, unsigned int line);
void gfree(void *data, char *file, unsigned int line);
void gfs2_memory_init(void);
void gfs2_memory_uninit(void);

#define gfs2_memory_add(data) \
gfs2_memory_add_i((data), __FILE__, __LINE__)
#define gfs2_memory_rm(data) \
gfs2_memory_rm_i((data), __FILE__, __LINE__)

#define kmalloc(size, flags) \
gmalloc((size), (flags), __FILE__, __LINE__)
#define kmalloc_nofail(size, flags) \
gmalloc_nofail((size), (flags), __FILE__, __LINE__)
#define kfree(data) \
gfree((data), __FILE__, __LINE__)

#else
void *gmalloc_nofail_real(unsigned int size, int flags,
			  char *file, unsigned int line);
#define gfs2_memory_add(x) do {} while (0)
#define gfs2_memory_rm(x) do {} while (0)
#define kmalloc_nofail(size, flags) \
gmalloc_nofail_real((size), (flags), __FILE__, __LINE__)
#define gfs2_memory_init() do {} while (0)
#define gfs2_memory_uninit() do {} while (0)
#endif


#endif /* __DEBUG_DOT_H__ */
