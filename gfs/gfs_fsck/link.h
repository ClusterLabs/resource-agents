#ifndef _LINK_H
#define _LINK_H

int set_link_count(struct fsck_sb *sbp, uint64_t inode_no, uint32_t count);
int increment_link(struct fsck_sb *sbp, uint64_t inode_no);
int decrement_link(struct fsck_sb *sbp, uint64_t inode_no);

#endif /* _LINK_H */
