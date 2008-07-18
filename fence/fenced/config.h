#ifndef __CONFIG_DOT_H__
#define __CONFIG_DOT_H__

#define DEFAULT_GROUPD_COMPAT 1
#define DEFAULT_DEBUG_LOGSYS 0
#define DEFAULT_CLEAN_START 0
#define DEFAULT_POST_JOIN_DELAY 6
#define DEFAULT_POST_FAIL_DELAY 0
#define DEFAULT_OVERRIDE_TIME 3
#define DEFAULT_OVERRIDE_PATH "/var/run/cluster/fenced_override"

extern int optd_groupd_compat;
extern int optd_debug_logsys;
extern int optd_clean_start;
extern int optd_post_join_delay;
extern int optd_post_fail_delay;
extern int optd_override_time;
extern int optd_override_path;

extern int cfgd_groupd_compat;
extern int cfgd_debug_logsys;
extern int cfgd_clean_start;
extern int cfgd_post_join_delay;
extern int cfgd_post_fail_delay;
extern int cfgd_override_time;
extern char *cfgd_override_path;

#endif

