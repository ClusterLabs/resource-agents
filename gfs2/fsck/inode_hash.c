#include <stdint.h>
#include <unistd.h>

#include "libgfs2.h"
#include "osi_list.h"
#include "hash.h"
#include "inode_hash.h"
#include "fsck.h"

static uint32_t gfs2_inode_hash(uint64_t block_no)
{
	unsigned int h;

	h = fsck_hash(&block_no, sizeof (uint64_t));
	h &= FSCK_HASH_MASK;

	return h;
}

struct inode_info *inode_hash_search(osi_list_t *buckets, uint64_t key)
{
	struct inode_info *ii;
	osi_list_t *tmp;
	osi_list_t *bucket = &buckets[gfs2_inode_hash(key)];

	osi_list_foreach(tmp, bucket) {
		ii = osi_list_entry(tmp, struct inode_info, list);
		if(ii->inode == key) {
			return ii;
		}
	}
	return NULL;
}

int inode_hash_insert(osi_list_t *buckets, uint64_t key, struct inode_info *ii)
{
	osi_list_t *tmp;
	osi_list_t *bucket = &buckets[gfs2_inode_hash(key)];
	struct inode_info *itmp = NULL;

	if(osi_list_empty(bucket)) {
		osi_list_add(&ii->list, bucket);
		return 0;
	}

	osi_list_foreach(tmp, bucket) {
		itmp = osi_list_entry(tmp, struct inode_info, list);
		if(itmp->inode < key) {
			continue;
		} else {
			osi_list_add_prev(&ii->list, tmp);
			return 0;
		}
	}
	osi_list_add_prev(&ii->list, bucket);
	return 0;
}


int inode_hash_remove(osi_list_t *buckets, uint64_t key)
{
	osi_list_t *tmp;
	osi_list_t *bucket = &buckets[gfs2_inode_hash(key)];
	struct inode_info *itmp = NULL;

	if(osi_list_empty(bucket)) {
		return -1;
	}
	osi_list_foreach(tmp, bucket) {
		itmp = osi_list_entry(tmp, struct inode_info, list);
		if(itmp->inode == key) {
			osi_list_del(tmp);
			return 0;
		}
	}
	return -1;
}
