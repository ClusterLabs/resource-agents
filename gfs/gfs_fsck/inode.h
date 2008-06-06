#ifndef _INODE_H
#define _INODE_H


int copyin_inode(struct fsck_sb *sbp, osi_buf_t *bh, struct fsck_inode **ip);
int load_inode(struct fsck_sb *sbp, uint64_t block, struct fsck_inode **ip);
void free_inode(struct fsck_inode **inode);
int check_inode(struct fsck_inode *ip);
int create_inode(struct fsck_sb *sbp, unsigned int type,
		 struct fsck_inode **ip);


#endif /* _INODE_H */
