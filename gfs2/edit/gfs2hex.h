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

#ifndef __GFS2HEX_DOT_H__
#define __GFS2HEX_DOT_H__


int display_gfs2(void);
int edit_gfs2(void);
void do_dinode_extended(struct gfs2_dinode *di, char *buf);
void print_gfs2(const char *fmt, ...);

#endif /*  __GFS2HEX_DOT_H__  */
