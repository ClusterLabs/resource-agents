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

#ifndef __ALLOCATION_H_
#define __ALLOCATION_H_

#include <stdlib.h>

#ifdef DEBUG
void *zalloc_debug(size_t size, char *filename, int line);
void free_debug(void *ptr, char *filename, int line);
int check_mem_bounds(void);
void print_meminfo_debug(void);

#define gfsck_zalloc(size) \
        zalloc_debug((size), __FILE__, __LINE__)
#define gfsck_free(ptr) \
        free_debug((ptr), __FILE__, __LINE__)
#define print_meminfo print_meminfo_debug
#define check_mem check_mem_bounds
#else
void *zalloc(size_t size);

#define gfsck_zalloc(size) \
        zalloc((size))
#define gfsck_free(ptr) \
        free((ptr))
#define print_meminfo
#define check_mem
#endif

#endif /* __ALLOCATION_H_ */
