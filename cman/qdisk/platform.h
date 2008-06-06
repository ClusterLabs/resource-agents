/** @file
 * Defines for byte-swapping
 */
#ifndef __PLATFORM_H
#define __PLATFORM_H

#include <endian.h>
#include <sys/param.h>
#include <byteswap.h>
#include <bits/wordsize.h>

/* No swapping on little-endian machines */
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define le_swap16(x) (x)
#define le_swap32(x) (x)
#define le_swap64(x) (x)
#else
#define le_swap16(x) bswap_16(x)
#define le_swap32(x) bswap_32(x)
#define le_swap64(x) bswap_64(x)
#endif

/* No swapping on big-endian machines */
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define be_swap16(x) bswap_16(x)
#define be_swap32(x) bswap_32(x)
#define be_swap64(x) bswap_64(x)
#else
#define be_swap16(x) (x)
#define be_swap32(x) (x)
#define be_swap64(x) (x)
#endif


#define swab16(x) x=be_swap16(x)
#define swab32(x) x=be_swap32(x)
#define swab64(x) x=be_swap64(x)


#endif /* __PLATFORM_H */
