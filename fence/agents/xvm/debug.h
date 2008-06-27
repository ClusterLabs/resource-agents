#ifndef _DBG_H
#define _DBG_H

#include <openais/service/logsys.h>

inline void dset(int);
inline int dget(void);

/*
 * Intentionally uses LOG_INFO for debugging if debugging is specified
 * because logsys's debugging level dumps immense amounts of noise to 
 * the log (such as the full path to the file at compile-time and line #)
 *
 * Specify >10 if you want this information.
 */

#define dbg_printf(level, fmt, args...) \
do { \
	if (dget()>=level) \
		log_printf(dget()>10?LOG_DEBUG:LOG_INFO, fmt, ##args); \
} while(0)

#endif
