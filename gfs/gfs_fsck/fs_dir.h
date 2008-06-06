#ifndef __FS_DIR_H__
#define __FS_DIR_H__

#include "osi_user.h"
#include "fsck_incore.h"

#define ID_FILENAME 0
#define ID_INUM     1
typedef struct identifier_s {
	int type;

	osi_filename_t *filename;
	struct gfs_inum *inum;
} identifier_t;

int dirent_del(struct fsck_inode *dip, osi_buf_t *bh,
	       struct gfs_dirent *prev, struct gfs_dirent *cur);
int fsck_inode_is_stuffed(struct fsck_inode *ip);
int dirent_first(osi_buf_t *bh, struct gfs_dirent **dent);
int get_leaf_nr(struct fsck_inode *dip, uint32 index, uint64 *leaf_out);
int put_leaf_nr(struct fsck_inode *dip, uint32 index, uint64 leaf_out);
int fs_filecmp(osi_filename_t *file1, char *file2, int len_of_file2);
int fs_dirent_del(struct fsck_inode *dip, osi_buf_t *bh, osi_filename_t *filename);
int fs_dir_add(struct fsck_inode *dip, osi_filename_t *filename,
	       struct gfs_inum *inum, unsigned int type);
int fs_dirent_alloc(struct fsck_inode *dip, osi_buf_t *bh,
		    int name_len, struct gfs_dirent **dent_out);

int fs_dir_search(struct fsck_inode *dip, identifier_t *id, unsigned int *type);
int dirent_repair(struct fsck_inode *ip, osi_buf_t *bh, struct gfs_dirent *de, 
		  struct gfs_dirent *dent, int type, int first);

#endif /* __FS_DIR_H__ */
