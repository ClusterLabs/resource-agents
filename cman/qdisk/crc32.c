/** @file
 * Calculates CRC32s on data.
 */
#include <stdint.h>
#include <sys/types.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#ifndef NO_ZLIB
#include <zlib.h>

/**
 * Calculate CRC32 of a data set.
 *
 * @param data		Data set for building CRC32
 * @param count		Size of data set, in bytes.
 * @return 		CRC32 of data set.
 */
uint32_t clu_crc32(const char *data, size_t count)
{
	return (uint32_t)crc32(0L, (const Bytef *)data, (uInt)count);
}

#else

#error "zlib is your friend."

#endif
