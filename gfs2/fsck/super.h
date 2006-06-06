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

#ifndef _SUPER_H
#define _SUPER_H

#include "libgfs2.h"

int read_sb(struct gfs2_sbd *sdp);
int ji_update(struct gfs2_sbd *sdp);
int ri_update(struct gfs2_sbd *sdp);
int write_sb(struct gfs2_sbd *sdp);

#endif /* _SUPER_H */
