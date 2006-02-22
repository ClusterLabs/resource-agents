/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

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

