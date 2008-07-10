#ifndef __CONFIG_DOT_H__
#define __CONFIG_DOT_H__

/* the kernel has default values for debug, timewarn and protocol;
   we only change them if new values are given on command line or in ccs */

#define DEFAULT_GROUPD_COMPAT 1
#define DEFAULT_ENABLE_FENCING 1
#define DEFAULT_ENABLE_QUORUM 1
#define DEFAULT_ENABLE_DEADLK 0
#define DEFAULT_ENABLE_PLOCK 1
#define DEFAULT_PLOCK_DEBUG 0
#define DEFAULT_PLOCK_RATE_LIMIT 100
#define DEFAULT_PLOCK_OWNERSHIP 1
#define DEFAULT_DROP_RESOURCES_TIME 10000 /* 10 sec */
#define DEFAULT_DROP_RESOURCES_COUNT 10
#define DEFAULT_DROP_RESOURCES_AGE 10000 /* 10 sec */

extern int optk_debug;
extern int optk_timewarn;
extern int optk_protocol;
extern int optd_groupd_compat;
extern int optd_enable_fencing;
extern int optd_enable_quorum;
extern int optd_enable_deadlk;
extern int optd_enable_plock;
extern int optd_plock_debug;
extern int optd_plock_rate_limit;
extern int optd_plock_ownership;
extern int optd_drop_resources_time;
extern int optd_drop_resources_count;
extern int optd_drop_resources_age;

extern int cfgk_debug;
extern int cfgk_timewarn;
extern int cfgk_protocol;
extern int cfgd_groupd_compat;
extern int cfgd_enable_fencing;
extern int cfgd_enable_quorum;
extern int cfgd_enable_deadlk;
extern int cfgd_enable_plock;
extern int cfgd_plock_debug;
extern int cfgd_plock_rate_limit;
extern int cfgd_plock_ownership;
extern int cfgd_drop_resources_time;
extern int cfgd_drop_resources_count;
extern int cfgd_drop_resources_age;

#endif

