#ifndef _INODE_HASH_H
#define _INODE_HASH_H

struct inode_info *inode_hash_search(osi_list_t *buckets, uint64_t block_no);
int inode_hash_insert(osi_list_t *buckets, uint64_t key, struct inode_info *ii);int inode_hash_remove(osi_list_t *buckets, uint64_t key);

#endif /* _INODE_HASH_H */
