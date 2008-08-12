#ifndef _DBG_H
#define _DBG_H

#include <corosync/engine/logsys.h>

inline void dset(int);
inline int dget(void);

#define dbg_printf(level, fmt, args...) \
do { \
	if (dget()>=level) \
		log_printf(LOG_DEBUG, fmt, ##args); \
} while(0)

#endif
