/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __EATTR_DOT_H__
#define __EATTR_DOT_H__

#define GFS_EA_MAY_WRITE 1
#define GFS_EA_MAY_READ 2

#define GFS_EA_DATA_LEN(x) gfs32_to_cpu((x)->ea_data_len)
#define GFS_EA_IS_UNSTUFFED(x) ((x)->ea_num_ptrs)
#define GFS_EA_DATA(x) ((char *)(x) + sizeof(struct gfs_ea_header) + (x)->ea_name_len)

struct gfs_ea_location {
	struct buffer_head *bh;
	struct gfs_ea_header *ea;
	struct gfs_ea_header *prev;
};

#define GFS_POSIX_ACL_ACCESS  "posix_acl_access"
#define GFS_POSIX_ACL_ACCESS_LEN 16
#define GFS_POSIX_ACL_DEFAULT "posix_acl_default"
#define GFS_POSIX_ACL_DEFAULT_LEN 17

#define IS_ACCESS_ACL(name, len) \
        ((len) == GFS_POSIX_ACL_ACCESS_LEN && \
         !memcmp(GFS_POSIX_ACL_ACCESS, (name), (len)))

#define IS_DEFAULT_ACL(name, len) \
        ((len) == GFS_POSIX_ACL_DEFAULT_LEN && \
         !memcmp(GFS_POSIX_ACL_DEFAULT, (name), (len)))

#define GFS_MAX_EA_ACL_BLKS 66	/* 65 for unstuffed data blocks, 1 for the ea
				   itself */

typedef int (*gfs_ea_copy_fn_t) (void *dest, void *src, unsigned long size);

int gfs_ea_memcpy(void *dest, void *src, unsigned long size);
int gfs_ea_copy_to_user(void *dest, void *src, unsigned long size);

int find_sys_space(struct gfs_inode *alloc_ip, struct gfs_inode *ip, int size,
		   struct gfs_ea_location *avail);

struct gfs_ea_header *prep_ea(struct gfs_ea_header *ea);

int write_ea(struct gfs_sbd *sdp, struct gfs_inode *alloc_ip,
	     struct gfs_inode *ip, struct gfs_ea_header *ea,
	     struct gfs_easet_io *req);

int gfs_get_eattr(struct gfs_sbd *sdp, struct gfs_inode *ip,
		  struct gfs_eaget_io *req, gfs_ea_copy_fn_t copy_fn);
int gfs_set_eattr(struct gfs_sbd *sdp, struct gfs_inode *ip,
		  struct gfs_easet_io *req);

int gfs_set_eattr_ioctl(struct gfs_sbd *sdp, struct gfs_inode *ip, void *arg);
int gfs_get_eattr_ioctl(struct gfs_sbd *sdp, struct gfs_inode *ip, void *arg);

int gfs_ea_dealloc(struct gfs_inode *ip);

int gfs_get_eattr_meta(struct gfs_inode *ip, struct gfs_user_buffer *ub);

int replace_ea(struct gfs_sbd *sdp, struct gfs_inode *ip,
	       struct gfs_ea_header *ea, struct gfs_easet_io *req);

int find_eattr(struct gfs_inode *ip, char *name, int name_len, int type,
	       struct gfs_ea_location *location);

int read_unstuffed(void *dest, struct gfs_inode *ip, struct gfs_sbd *sdp,
		   struct gfs_ea_header *ea, uint32_t avail_size,
		   gfs_ea_copy_fn_t copy_fn);

int get_ea(struct gfs_sbd *sdp, struct gfs_inode *ip, struct gfs_eaget_io *req,
	   gfs_ea_copy_fn_t copy_fn);

int init_new_inode_eattr(struct gfs_inode *dip, struct gfs_inode *ip,
			 struct gfs_easet_io *req);

int gfs_ea_read_permission(struct gfs_eaget_io *req, struct gfs_inode *ip);

#endif /* __EATTR_DOT_H__ */
