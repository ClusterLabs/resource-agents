#include <stdio.h>
#include "debug.h"

LOGSYS_DECLARE_SUBSYS("XVM", LOG_LEVEL_NOTICE);

static int _debug = 0;

inline void
dset(int threshold)
{
	_debug = threshold;
	dbg_printf(3, "Debugging threshold is now %d\n", threshold);
}

inline int
dget(void)
{
	return _debug;
}

