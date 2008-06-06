#ifndef _HASH_H
#define _HASH_H

uint32_t fsck_hash(const void *data, unsigned int len);
uint32_t fsck_hash_more(const void *data, unsigned int len, uint32_t hash);

#endif				/* _HASH_H  */
