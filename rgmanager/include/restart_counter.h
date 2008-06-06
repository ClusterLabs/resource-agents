/* Time-based restart counters for rgmanager */

#ifndef _RESTART_COUNTER_H
#define _RESTART_COUNTER_H

typedef void *restart_counter_t;

int restart_add(restart_counter_t arg);
int restart_clear(restart_counter_t arg);
int restart_count(restart_counter_t arg);
restart_counter_t restart_init(time_t expire_timeout, int max_restarts);
int restart_cleanup(restart_counter_t arg);

#endif
