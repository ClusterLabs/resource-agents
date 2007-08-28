/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005-2007 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

extern void log_msg(int priority, char *fmt, ...);
extern void init_debug(int subsystems);
extern void set_debuglog(int subsystems);

/* Debug macros */
#define CMAN_DEBUG_NONE    1
#define CMAN_DEBUG_BARRIER 2
#define CMAN_DEBUG_MEMB    4
#define CMAN_DEBUG_DAEMON  8
#define CMAN_DEBUG_AIS    16

extern void log_debug(int subsys, int stamp, const char *fmt, ...);

#define P_BARRIER(fmt, args...) log_debug(CMAN_DEBUG_BARRIER, 1, fmt, ## args)
#define P_MEMB(fmt, args...)    log_debug(CMAN_DEBUG_MEMB, 1, fmt, ## args)
#define P_DAEMON(fmt, args...)  log_debug(CMAN_DEBUG_DAEMON, 1, fmt, ## args)
#define P_AIS(fmt, args...)     log_debug(CMAN_DEBUG_AIS, 1, fmt, ## args)
