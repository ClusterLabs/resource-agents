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
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

#include "linux_endian.h"
#include "gfs_ondisk.h"

#define printk printf
#define pv(struct, member, fmt) printf("  "#member" = "fmt"\n", struct->member);

#define ENTER(x)
#define EXIT(x)
#define RET(x) return
#define RETURN(x, y) return y

#define WANT_GFS_CONVERSION_FUNCTIONS
#include "gfs_ondisk.h"
