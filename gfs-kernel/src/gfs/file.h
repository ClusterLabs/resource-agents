#ifndef __FILE_DOT_H__
#define __FILE_DOT_H__

typedef int (*read_copy_fn_t) (struct buffer_head *bh, void **buf,
			       unsigned int offset, unsigned int size);
typedef int (*write_copy_fn_t) (struct gfs_inode *ip, struct buffer_head *bh,
				void **buf, unsigned int offset,
				unsigned int size, int new);

int gfs_copy2mem(struct buffer_head *bh, void **buf,
		 unsigned int offset, unsigned int size);
int gfs_copy2user(struct buffer_head *bh, void **buf,
		  unsigned int offset, unsigned int size);
int gfs_readi(struct gfs_inode *ip, void *buf, uint64_t offset,
	      unsigned int size, read_copy_fn_t copy_fn);

int gfs_copy_from_mem(struct gfs_inode *ip, struct buffer_head *bh, void **buf,
		      unsigned int offset, unsigned int size, int new);
int gfs_copy_from_user(struct gfs_inode *ip, struct buffer_head *bh, void **buf,
		       unsigned int offset, unsigned int size, int new);
int gfs_writei(struct gfs_inode *ip, void *buf, uint64_t offset,
               unsigned int size, write_copy_fn_t copy_fn,
               struct kiocb *iocb);

int gfs_zero_blocks(struct gfs_inode *ip, struct buffer_head *bh, void **buf,
		    unsigned int offset, unsigned int size, int new);

static __inline__ int
gfs_internal_read(struct gfs_inode *ip, char *buf, uint64_t offset,
		  unsigned int size)
{
	return gfs_readi(ip, buf, offset, size, gfs_copy2mem);
}

static __inline__ int
gfs_internal_write(struct gfs_inode *ip, char *buf, uint64_t offset,
		   unsigned int size)
{
	return gfs_writei(ip, buf, offset, size, gfs_copy_from_mem, NULL);
}

#endif /* __FILE_DOT_H__ */
