/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __GFS2_DOT_H__
#define __GFS2_DOT_H__

#include <linux/lm_interface.h>
#include <linux/gfs2_ondisk.h>

#include "fixed_div64.h"
#include "lvb.h"
#include "incore.h"
#include "util.h"
#include "debug.h"

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define NO_CREATE 0
#define CREATE 1

#define NO_WAIT 0
#define WAIT 1

#define NO_FORCE 0
#define FORCE 1

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

#define GFS2_FAST_NAME_SIZE 8

#define get_v2sdp(sb) ((struct gfs2_sbd *)(sb)->s_fs_info)
#define set_v2sdp(sb, sdp) (sb)->s_fs_info = (sdp)
#define get_v2ip(inode) ((struct gfs2_inode *)(inode)->u.generic_ip)
#define set_v2ip(inode, ip) (inode)->u.generic_ip = (ip)
#define get_v2fp(file) ((struct gfs2_file *)(file)->private_data)
#define set_v2fp(file, fp) (file)->private_data = (fp)
#define get_v2bd(bh) ((struct gfs2_bufdata *)(bh)->b_private)
#define set_v2bd(bh, bd) (bh)->b_private = (bd)
#define get_v2db(bh) ((struct gfs2_databuf *)(bh)->b_private)
#define set_v2db(bh, db) (bh)->b_private = (db)

#define get_transaction ((struct gfs2_trans *)(current->journal_info))
#define set_transaction(tr) (current->journal_info) = (tr)

#define get_gl2ip(gl) ((struct gfs2_inode *)(gl)->gl_object)
#define set_gl2ip(gl, ip) (gl)->gl_object = (ip)
#define get_gl2rgd(gl) ((struct gfs2_rgrpd *)(gl)->gl_object)
#define set_gl2rgd(gl, rgd) (gl)->gl_object = (rgd)
#define get_gl2gl(gl) ((struct gfs2_glock *)(gl)->gl_object)
#define set_gl2gl(gl, gl2) (gl)->gl_object = (gl2)

#endif /* __GFS2_DOT_H__ */

