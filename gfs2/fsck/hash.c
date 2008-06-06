/* This is the same hash algorithm used by the glocks in gfs */

#include <stdint.h>
#include <unistd.h>
#include "libgfs2.h"
#include "hash.h"
#include "osi_list.h"

/**
 * hash_more_internal - hash an array of data
 * @data: the data to be hashed
 * @len: the length of data to be hashed
 * @hash: the hash from a previous call
 *
 * Take some data and convert it to a 32-bit hash.
 *
 * This is the 32-bit FNV-1a hash from:
 * http://www.isthe.com/chongo/tech/comp/fnv/
 *
 * Hash guts
 *
 * Returns: the hash
 */

static __inline__ uint32_t
hash_more_internal(const void *data, unsigned int len, uint32_t hash)
{
	unsigned char *p = (unsigned char *) data;
	unsigned char *e = p + len;
	uint32_t h = hash;

	while (p < e) {
		h ^= (uint32_t) (*p++);
		h *= 0x01000193;
	}

	return h;
}

/**
 * fsck_hash - hash an array of data
 * @data: the data to be hashed
 * @len: the length of data to be hashed
 *
 * Take some data and convert it to a 32-bit hash.
 *
 * This is the 32-bit FNV-1a hash from:
 * http://www.isthe.com/chongo/tech/comp/fnv/
 *
 * Returns: the hash
 */

uint32_t
fsck_hash(const void *data, unsigned int len)
{
	uint32_t h = 0x811C9DC5;
	h = hash_more_internal(data, len, h);
	return h;
}

/**
 * fsck_hash_more - hash an array of data
 * @data: the data to be hashed
 * @len: the length of data to be hashed
 * @hash: the hash from a previous call
 *
 * Take some data and convert it to a 32-bit hash.
 *
 * This is the 32-bit FNV-1a hash from:
 * http://www.isthe.com/chongo/tech/comp/fnv/
 *
 * This version let's you hash together discontinuous regions.
 * For example, to compute the combined hash of the memory in
 * (data1, len1), (data2, len2), and (data3, len3) you:
 *
 *   h = fsck_hash(data1, len1);
 *   h = fsck_hash_more(data2, len2, h);
 *   h = fsck_hash_more(data3, len3, h);
 *
 * Returns: the hash
 */

uint32_t
fsck_hash_more(const void *data, unsigned int len, uint32_t hash)
{
	uint32_t h;
	h = hash_more_internal(data, len, hash);
	return h;
}


