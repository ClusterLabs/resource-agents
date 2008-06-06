#ifndef __FS_INODE_H__
#define __FS_INODE_H__

#include "fsck_incore.h"

int fs_copyin_dinode(struct fsck_inode *ip, osi_buf_t *bh);
int fs_copyout_dinode(struct fsck_inode *ip);
int fs_mkdir(struct fsck_inode *dip, char *new_dir, int mode, struct fsck_inode **nip);
int fs_remove(struct fsck_inode *ip);

static __inline__ int fs_is_stuffed(struct fsck_inode *ip)
{
	return !ip->i_di.di_height;
}

static __inline__ int fs_is_jdata(struct fsck_inode *ip)
{
	return ip->i_di.di_flags & GFS_DIF_JDATA;
}


#endif /*  __FS_INODE_H__ */
