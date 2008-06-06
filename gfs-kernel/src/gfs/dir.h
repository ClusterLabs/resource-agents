#ifndef __DIR_DOT_H__
#define __DIR_DOT_H__

/**
 * gfs_filldir_t - Report a directory entry to the caller of gfs_dir_read()
 * @opaque: opaque data used by the function
 * @name: the name of the directory entry
 * @length: the length of the name
 * @offset: the entry's offset in the directory
 * @inum: the inode number the entry points to
 * @type: the type of inode the entry points to
 *
 * Returns: 0 on success, 1 if buffer full
 */

typedef int (*gfs_filldir_t) (void *opaque,
			      const char *name, unsigned int length,
			      uint64_t offset,
			      struct gfs_inum *inum, unsigned int type);

int gfs_filecmp(struct qstr *file1, char *file2, int len_of_file2);
int gfs_dirent_alloc(struct gfs_inode *dip, struct buffer_head *bh,
		     int name_len, struct gfs_dirent **dent_out);

int gfs_dir_search(struct gfs_inode *dip, struct qstr *filename,
		   struct gfs_inum *inum, unsigned int *type);
int gfs_dir_add(struct gfs_inode *dip, struct qstr *filename,
		struct gfs_inum *inum, unsigned int type);
int gfs_dir_del(struct gfs_inode *dip, struct qstr *filename);
int gfs_dir_read(struct gfs_inode *dip, uint64_t * offset, void *opaque,
		 gfs_filldir_t filldir);
int gfs_dir_mvino(struct gfs_inode *dip, struct qstr *filename,
		  struct gfs_inum *new_inum, unsigned int new_type);

int gfs_dir_exhash_free(struct gfs_inode *dip);

int gfs_diradd_alloc_required(struct gfs_inode *dip, struct qstr *filename,
			      int *alloc_required);

int gfs_get_dir_meta(struct gfs_inode *ip, struct gfs_user_buffer *ub);

#endif /* __DIR_DOT_H__ */
