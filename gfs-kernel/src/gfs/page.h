#ifndef __PAGE_DOT_H__
#define __PAGE_DOT_H__

void gfs_inval_pte(struct gfs_glock *gl);
void gfs_inval_page(struct gfs_glock *gl);
void gfs_sync_page_i(struct inode *inode, int flags);
void gfs_sync_page(struct gfs_glock *gl, int flags);

int gfs_unstuffer_page(struct gfs_inode *ip, struct buffer_head *dibh,
		       uint64_t block, void *private);
int gfs_truncator_page(struct gfs_inode *ip, uint64_t size);

#endif /* __PAGE_DOT_H__ */
