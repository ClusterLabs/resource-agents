#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

#include "linux_endian.h"

#define printk printf

#define WANT_GFS2_CONVERSION_FUNCTIONS
#include <linux/gfs2_ondisk.h>
