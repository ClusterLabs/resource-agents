#ifndef _DBG_H
#define _DBG_H

inline void dset(int);
inline int dget(void);

#define dbg_printf(level, fmt, args...) \
do { \
	if (dget()>=level) \
		printf(fmt, ##args); \
} while(0)

#endif
