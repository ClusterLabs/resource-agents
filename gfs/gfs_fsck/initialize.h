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

#ifndef __INITIALIZE_H__
#define __INITIALIZE_H__

#include "fs_incore.h"

int initialize(fs_sbd_t **sdp, char *device);
void cleanup(fs_sbd_t **sdp);

#endif /* __INITIALIZE_H__ */
