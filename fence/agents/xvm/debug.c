#include <stdio.h>
#include "debug.h"

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
