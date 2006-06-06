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

#ifndef __UTIL_H__
#define __UTIL_H__

#include "libgfs2.h"

#define do_lseek(fd, off) \
  ((lseek((fd), (off), SEEK_SET) == (off)) ? 0 : -1)

#define do_read(fd, buff, len) \
  ((read((fd), (buff), (len)) == (len)) ? 0 : -1)

#define do_write(fd, buff, len) \
  ((write((fd), (buff), (len)) == (len)) ? 0 : -1)


int compute_height(struct gfs2_sbd *sdp, uint64_t sz);
struct di_info *search_list(osi_list_t *list, uint64_t addr);
void warm_fuzzy_stuff(uint64_t block);
const char *block_type_string(struct gfs2_block_query *q);

#endif /* __UTIL_H__ */
