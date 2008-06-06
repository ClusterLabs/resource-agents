#ifndef __CONFIG_DOT_H__
#define __CONFIG_DOT_H__

#define DEFAULT_GROUPD_COMPAT 1
#define DEFAULT_ENABLE_WITHDRAW 1
#define DEFAULT_ENABLE_PLOCK 1
#define DEFAULT_PLOCK_DEBUG 0
#define DEFAULT_PLOCK_RATE_LIMIT 100
#define DEFAULT_PLOCK_OWNERSHIP 1
#define DEFAULT_DROP_RESOURCES_TIME 10000 /* 10 sec */
#define DEFAULT_DROP_RESOURCES_COUNT 10
#define DEFAULT_DROP_RESOURCES_AGE 10000 /* 10 sec */

extern int optd_groupd_compat;
extern int optd_enable_withdraw;
extern int optd_enable_plock;
extern int optd_plock_debug;
extern int optd_plock_rate_limit;
extern int optd_plock_ownership;
extern int optd_drop_resources_time;
extern int optd_drop_resources_count;
extern int optd_drop_resources_age;

extern int cfgd_groupd_compat;
extern int cfgd_enable_withdraw;
extern int cfgd_enable_plock;
extern int cfgd_plock_debug;
extern int cfgd_plock_rate_limit;
extern int cfgd_plock_ownership;
extern int cfgd_drop_resources_time;
extern int cfgd_drop_resources_count;
extern int cfgd_drop_resources_age;

#endif

