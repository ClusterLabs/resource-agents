/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __ACL_DOT_H__
#define __ACL_DOT_H__

int gfs_setup_new_acl(struct gfs_inode *dip,
		      unsigned int type, unsigned int *mode,
		      struct posix_acl **acl_ptr);
int gfs_getacl(struct inode *inode, int access, struct posix_acl **acl_ptr);
int gfs_init_acl(struct gfs_inode *dip, struct gfs_inode *ip, unsigned int type,
		 struct posix_acl *acl);
int gfs_acl_setattr(struct inode *inode);
int gfs_validate_acl(struct gfs_inode *ip, const char *value, int size,
                     int access);
void gfs_acl_set_mode(struct gfs_inode *ip, struct posix_acl *acl);

#endif /* __ACL_DOT_H__ */
