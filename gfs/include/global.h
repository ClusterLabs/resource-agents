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

#ifndef __GLOBAL_DOT_H__
#define __GLOBAL_DOT_H__

#ifdef __cplusplus
extern "C" {
#endif


#if defined(__KERNEL__) || defined(_KERNEL)
#error "don't use global.h in kernel space"
#endif



#ifndef TRUE
#define TRUE (1)
#endif

#ifndef FALSE
#define FALSE (0)
#endif



#include <inttypes.h>

typedef uint64_t          uint64;
typedef uint32_t          uint32;
typedef uint16_t          uint16;
typedef uint8_t           uint8;
typedef int64_t           int64;
typedef int32_t           int32;
typedef int16_t           int16;
typedef int8_t            int8;



#ifdef __cplusplus
}
#endif

#endif /* __GLOBAL_H__ */
