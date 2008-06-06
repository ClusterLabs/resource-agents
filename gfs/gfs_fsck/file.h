#ifndef _FILE_H
#define _FILE_H

#include <stdint.h>
#include "fsck_incore.h"

int readi(struct fsck_inode *ip, void *buf, uint64_t offset, unsigned int size);
int writei(struct fsck_inode *ip, void *buf, uint64_t offset, unsigned int size);

#endif /* _FILE_H */
