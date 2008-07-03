#include <openais/service/logsys.h>

extern void set_debuglog(int subsystems);

/* Debug macros */
#define CMAN_DEBUG_NONE    1
#define CMAN_DEBUG_BARRIER 2
#define CMAN_DEBUG_MEMB    4
#define CMAN_DEBUG_DAEMON  8
#define CMAN_DEBUG_AIS    16

extern int subsys_mask;

#define P_BARRIER(fmt, args...) if (subsys_mask & CMAN_DEBUG_BARRIER) log_printf(LOG_LEVEL_DEBUG, "barrier: " fmt, ## args)
#define P_MEMB(fmt, args...)    if (subsys_mask & CMAN_DEBUG_MEMB) log_printf(LOG_LEVEL_DEBUG, "memb: " fmt, ## args)
#define P_DAEMON(fmt, args...)  if (subsys_mask & CMAN_DEBUG_DAEMON) log_printf(LOG_LEVEL_DEBUG , "daemon: " fmt, ## args)
#define P_AIS(fmt, args...)     if (subsys_mask & CMAN_DEBUG_AIS) log_printf(LOG_LEVEL_DEBUG, "ais " fmt, ## args)
