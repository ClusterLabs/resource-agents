#ifndef _DBG_H
#define _DBG_H

#include <liblogthread.h>

inline void dset(int);
inline int dget(void);

#define dbg_printf(level, fmt, args...) \
do { \
	if (dget()>=level) \
		logt_print(LOG_DEBUG, fmt, ##args); \
} while(0)

#endif
