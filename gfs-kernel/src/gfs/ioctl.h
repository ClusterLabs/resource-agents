#ifndef __IOCTL_DOT_H__
#define __IOCTL_DOT_H__

int gfs_ioctl_i_local(struct gfs_inode *ip, struct gfs_ioctl *gi,
		      const char *arg0, int from_user);
int gfs_ioctl_i_compat(struct gfs_inode *ip, unsigned long arg);
int gfs_ioctl_i(struct gfs_inode *ip, void *arg);

#endif /* __IOCTL_DOT_H__ */
