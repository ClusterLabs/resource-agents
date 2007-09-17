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
#include <openais/service/logsys.h>
extern void init_debug(int subsystems);
extern void cman_flush_debuglog(void);
extern void set_debuglog(int subsystems);

/* Debug macros */
#define CMAN_DEBUG_NONE    1
#define CMAN_DEBUG_BARRIER 2
#define CMAN_DEBUG_MEMB    4
#define CMAN_DEBUG_DAEMON  8
#define CMAN_DEBUG_AIS    16

extern unsigned int logsys_subsys_id;
extern int subsys_mask;

#define P_BARRIER(fmt, args...) if (subsys_mask & CMAN_DEBUG_BARRIER) log_printf(logsys_mkpri(LOG_LEVEL_DEBUG, logsys_subsys_id), "barrier: " fmt, ## args)
#define P_MEMB(fmt, args...)    if (subsys_mask & CMAN_DEBUG_MEMB) log_printf(LOG_LEVEL_DEBUG , "memb: " fmt, ## args)
#define P_DAEMON(fmt, args...)  if (subsys_mask & CMAN_DEBUG_DAEMON) log_printf(LOG_LEVEL_DEBUG , "daemon: " fmt, ## args)
#define P_AIS(fmt, args...)     if (subsys_mask & CMAN_DEBUG_AIS) log_printf(LOG_LEVEL_DEBUG, "ais " fmt, ## args)
