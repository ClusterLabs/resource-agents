#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

#include "linux_endian.h"

#define printk printf

#define __be16 uint16_t
#define __be32 uint32_t
#define __be64 uint64_t
#define __u16 uint16_t
#define __u32 uint32_t
#define __u64 uint64_t
#define __u8 uint8_t

#define WANT_GFS2_CONVERSION_FUNCTIONS
#include <linux/gfs2_ondisk.h>

#include "gfs2_disk_hash.h"

