#ifndef __OPS_FSTYPE_DOT_H__
#define __OPS_FSTYPE_DOT_H__

int gfs_sys_init(void);
void gfs_sys_uninit(void);
void gfs_sys_fs_del(struct gfs_sbd *sdp);
int gfs_test_bdev_super(struct super_block *sb, void *data);
int gfs_set_bdev_super(struct super_block *sb, void *data);
int init_names(struct gfs_sbd *sdp, int silent);

extern struct file_system_type gfs_fs_type;

#endif /* __OPS_FSTYPE_DOT_H__ */
