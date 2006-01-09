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

#ifndef __GFS_DOT_H__
#define __GFS_DOT_H__

#define GFS_RELEASE_NAME "<CVS>"

#include <linux/gfs_ondisk.h>

#include "lm_interface.h"
#include "fixed_div64.h"
#include "lvb.h"
#include "incore.h"
#include "debug.h"
#include "util.h"

#ifndef TRUE
#define TRUE (1)
#endif

#ifndef FALSE
#define FALSE (0)
#endif

#define NO_CREATE (0)
#define CREATE (1)

#if (BITS_PER_LONG == 64)
#define PRIu64 "lu"
#define PRId64 "ld"
#define PRIo64 "lo"
#define PRIx64 "lx"
#define PRIX64 "lX"
#define SCNu64 "lu"
#define SCNd64 "ld"
#define SCNo64 "lo"
#define SCNx64 "lx"
#define SCNX64 "lX"
#else
#define PRIu64 "Lu"
#define PRId64 "Ld"
#define PRIo64 "Lo"
#define PRIx64 "Lx"
#define PRIX64 "LX"
#define SCNu64 "Lu"
#define SCNd64 "Ld"
#define SCNo64 "Lo"
#define SCNx64 "Lx"
#define SCNX64 "LX"
#endif

/*  Divide num by den.  Round up if there is a remainder.  */
#define DIV_RU(num, den) (((num) + (den) - 1) / (den))
#define MAKE_MULT8(x) (((x) + 7) & ~7)

#define GFS_FAST_NAME_SIZE (8)

#define vfs2sdp(sb) ((struct gfs_sbd *)(sb)->s_fs_info)
#define vn2ip(inode) ((struct gfs_inode *)(inode)->u.generic_ip)
#define vf2fp(file) ((struct gfs_file *)(file)->private_data)
#define bh2bd(bh) ((struct gfs_bufdata *)(bh)->b_private)

/* A process can build only one transaction at a time */
#define current_transaction ((struct gfs_trans *)(current->journal_info))

#define gl2ip(gl) ((struct gfs_inode *)(gl)->gl_object)
#define gl2rgd(gl) ((struct gfs_rgrpd *)(gl)->gl_object)
#define gl2gl(gl) ((struct gfs_glock *)(gl)->gl_object)

#endif /* __GFS_DOT_H__ */
