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

#ifndef __OSI_ENDIAN_DOT_H__
#define __OSI_ENDIAN_DOT_H__


#ifdef __linux__

#ifdef __KERNEL__
#error "don't use osi_endian.h in kernel space under Linux"
#endif

#include <endian.h>
#include <byteswap.h>

/*  I'm not sure which versions of alpha glibc/gcc are broken,
    so fix all of them.  */
#ifdef __alpha__
#undef bswap_64
static __inline__ unsigned long bswap_64(unsigned long x)
{
  unsigned int h = x >> 32;
  unsigned int l = x;

  h = bswap_32(h);
  l = bswap_32(l);

  return ((unsigned long)l << 32) | h;
}
#endif  /*  __alpha__  */

#endif /*  __linux__ */


#ifdef __FreeBSD__

#include <machine/endian.h>
#include <sys/types.h>

#define __BIG_ENDIAN BIG_ENDIAN
#define __LITTLE_ENDIAN LITTLE_ENDIAN
#define __BYTE_ORDER BYTE_ORDER

#define bswap_16 __byte_swap_word
#define bswap_32 __byte_swap_long

/* There is no 64 bit swap operation in FreeBSD, so define bswap_64 the
   same as the alpha fix for linux */
static __inline__ unsigned long long bswap_64(unsigned long long x)
{
  unsigned int h = x >> 32;
  unsigned int l = x;

  h = bswap_32(h);
  l = bswap_32(l);

  return ((unsigned long long)l << 32) | h;
}

#endif /*  __FreeBSD__ */


#if __BYTE_ORDER == __BIG_ENDIAN

#define osi_be16_to_cpu(x) (x)
#define osi_be32_to_cpu(x) (x)
#define osi_be64_to_cpu(x) (x)

#define osi_cpu_to_be16(x) (x)
#define osi_cpu_to_be32(x) (x)
#define osi_cpu_to_be64(x) (x)

#define osi_le16_to_cpu(x) (bswap_16((x)))
#define osi_le32_to_cpu(x) (bswap_32((x)))
#define osi_le64_to_cpu(x) (bswap_64((x)))

#define osi_cpu_to_le16(x) (bswap_16((x)))
#define osi_cpu_to_le32(x) (bswap_32((x)))
#define osi_cpu_to_le64(x) (bswap_64((x)))

#endif  /*  __BYTE_ORDER == __BIG_ENDIAN  */


#if __BYTE_ORDER == __LITTLE_ENDIAN

#define osi_be16_to_cpu(x) (bswap_16((x)))
#define osi_be32_to_cpu(x) (bswap_32((x)))
#define osi_be64_to_cpu(x) (bswap_64((x)))

#define osi_cpu_to_be16(x) (bswap_16((x)))
#define osi_cpu_to_be32(x) (bswap_32((x)))
#define osi_cpu_to_be64(x) (bswap_64((x))) 

#define osi_le16_to_cpu(x) (x)
#define osi_le32_to_cpu(x) (x)
#define osi_le64_to_cpu(x) (x)

#define osi_cpu_to_le16(x) (x)
#define osi_cpu_to_le32(x) (x)
#define osi_cpu_to_le64(x) (x)

#endif  /*  __BYTE_ORDER == __LITTLE_ENDIAN  */


#endif  /*  __OSI_ENDIAN_DOT_H__  */
