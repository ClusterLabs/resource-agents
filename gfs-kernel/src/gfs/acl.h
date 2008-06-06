#ifndef __ACL_DOT_H__
#define __ACL_DOT_H__

#define GFS_POSIX_ACL_ACCESS  "posix_acl_access"
#define GFS_POSIX_ACL_ACCESS_LEN (16)
#define GFS_POSIX_ACL_DEFAULT "posix_acl_default"
#define GFS_POSIX_ACL_DEFAULT_LEN (17)

#define GFS_ACL_IS_ACCESS(name, len) \
         ((len) == GFS_POSIX_ACL_ACCESS_LEN && \
         !memcmp(GFS_POSIX_ACL_ACCESS, (name), (len)))

#define GFS_ACL_IS_DEFAULT(name, len) \
         ((len) == GFS_POSIX_ACL_DEFAULT_LEN && \
         !memcmp(GFS_POSIX_ACL_DEFAULT, (name), (len)))

struct gfs_ea_request;

int gfs_acl_validate_set(struct gfs_inode *ip, int access,
			 struct gfs_ea_request *er,
			 int *remove, mode_t *mode);
int gfs_acl_validate_remove(struct gfs_inode *ip, int access);
int gfs_acl_get(struct gfs_inode *ip, int access, struct posix_acl **acl);
int gfs_check_acl(struct inode *inode, int mask);
int gfs_acl_new_prep(struct gfs_inode *dip,
		     unsigned int type, mode_t *mode,
		     void **a_data, void **d_data,
		     unsigned int *size,
		     unsigned int *blocks);
int gfs_acl_new_init(struct gfs_inode *dip, struct gfs_inode *ip,
		     void *a_data, void *d_data, unsigned int size);
int gfs_acl_chmod(struct gfs_inode *ip, struct iattr *attr);

#endif /* __ACL_DOT_H__ */
