#ifndef __EAOPS_DOT_H__
#define __EAOPS_DOT_H__

struct gfs_ea_request;

struct gfs_eattr_operations {
	int (*eo_get) (struct gfs_inode *ip, struct gfs_ea_request *er);
	int (*eo_set) (struct gfs_inode *ip, struct gfs_ea_request *er);
	int (*eo_remove) (struct gfs_inode *ip, struct gfs_ea_request *er);
	char *eo_name;
};

unsigned int gfs_ea_name2type(const char *name, char **truncated_name);

extern struct gfs_eattr_operations gfs_user_eaops;
extern struct gfs_eattr_operations gfs_system_eaops;

extern struct gfs_eattr_operations *gfs_ea_ops[];

#endif /* __EAOPS_DOT_H__ */

