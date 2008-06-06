#ifndef __LINUX_ENDIAN_DOT_H__
#define __LINUX_ENDIAN_DOT_H__


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


#if __BYTE_ORDER == __BIG_ENDIAN

#define be16_to_cpu(x) (x)
#define be32_to_cpu(x) (x)
#define be64_to_cpu(x) (x)

#define cpu_to_be16(x) (x)
#define cpu_to_be32(x) (x)
#define cpu_to_be64(x) (x)

#define le16_to_cpu(x) (bswap_16((x)))
#define le32_to_cpu(x) (bswap_32((x)))
#define le64_to_cpu(x) (bswap_64((x)))

#define cpu_to_le16(x) (bswap_16((x)))
#define cpu_to_le32(x) (bswap_32((x)))
#define cpu_to_le64(x) (bswap_64((x)))

#endif  /*  __BYTE_ORDER == __BIG_ENDIAN  */


#if __BYTE_ORDER == __LITTLE_ENDIAN

#define be16_to_cpu(x) (bswap_16((x)))
#define be32_to_cpu(x) (bswap_32((x)))
#define be64_to_cpu(x) (bswap_64((x)))

#define cpu_to_be16(x) (bswap_16((x)))
#define cpu_to_be32(x) (bswap_32((x)))
#define cpu_to_be64(x) (bswap_64((x))) 

#define le16_to_cpu(x) (x)
#define le32_to_cpu(x) (x)
#define le64_to_cpu(x) (x)

#define cpu_to_le16(x) (x)
#define cpu_to_le32(x) (x)
#define cpu_to_le64(x) (x)

#endif  /*  __BYTE_ORDER == __LITTLE_ENDIAN  */


#endif  /*  __LINUX_ENDIAN_DOT_H__  */
