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


#ifdef GFS_TRACE
int gfs_trace_enter(unsigned int flag);
void gfs_trace_exit(unsigned int flag, int dummy);
#define ENTER_TRACE(x) int gfs_trace_var = gfs_trace_enter((x));
#define EXIT_TRACE(x) gfs_trace_exit((x), gfs_trace_var);
int gfs_trace_init(void);
void gfs_trace_uninit(void);
#else
#define ENTER_TRACE(x)
#define EXIT_TRACE(x)
#endif


#ifdef GFS_PROFILE
extern void *gfs_profile_cookie;
#define ENTER_PROFILE(x) uint64_t gfs_profile_var = kdbl_profile_enter();
#define EXIT_PROFILE(x) kdbl_profile_exit(gfs_profile_cookie, (x), gfs_profile_var);
int gfs_profile_init(void);
void gfs_profile_uninit(void);
#else
#define ENTER_PROFILE(x)
#define EXIT_PROFILE(x)
#endif


#if defined(GFS_TRACE) || defined(GFS_PROFILE)
#include <linux/kdbl.h>
#include <linux/gfs_debug_const.h>
#define ENTER(x) ENTER_PROFILE(x) ENTER_TRACE(x)
#define EXIT(x) EXIT_TRACE(x) EXIT_PROFILE(x)
#define RET(x) \
do { \
	EXIT(x) \
	return; \
} while (0)
#define RETURN(x, y) \
do { \
	typeof(y) RETURN_var = y; \
	EXIT(x) \
	return RETURN_var; \
} while (0)
#else
#define ENTER(x)
#define EXIT(x)
#define RET(x) return
#define RETURN(x, y) return y
#endif
#define DBFLAG(x)


#endif /* __DEBUG_DOT_H__ */
