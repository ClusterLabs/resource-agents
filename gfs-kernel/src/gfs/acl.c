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

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <asm/semaphore.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/xattr_acl.h>

#include "gfs.h"
#include "acl.h"
#include "dio.h"
#include "eattr.h"
#include "glock.h"
#include "trans.h"
#include "inode.h"

/*
 * Check to make sure that the acl is actually valid
 */ 
int
gfs_validate_acl(struct gfs_inode *ip, const char *value, int size, int access)
{
	int err = 0;
	struct posix_acl *acl = NULL;
	struct gfs_sbd *sdp = ip->i_sbd;

	if ((current->fsuid != ip->i_di.di_uid) && !capable(CAP_FOWNER))
		return -EPERM;
	if (ip->i_di.di_type == GFS_FILE_LNK)
		return -EOPNOTSUPP;
	if (!access && ip->i_di.di_type != GFS_FILE_DIR)
		return -EACCES;
	if (!sdp->sd_args.ar_posixacls)
		return -EOPNOTSUPP;

	if (value) {
		acl = posix_acl_from_xattr(value, size);
		if (IS_ERR(acl))
			return PTR_ERR(acl);
		else if (acl) {
			err = posix_acl_valid(acl);
			posix_acl_release(acl);
		}
	}
	return err;     
}

void
gfs_acl_set_mode(struct gfs_inode *ip, struct posix_acl *acl)
{
	struct inode *inode;
	mode_t mode;

	inode = gfs_iget(ip, NO_CREATE);
	mode = inode->i_mode;
	posix_acl_equiv_mode(acl, &mode);
	inode->i_mode = mode;
	iput(inode);
	gfs_inode_attr_out(ip);
}


/**
 * gfs_replace_acl - replace the value of the ea to the value of the acl
 *
 * NOTE: The new value must be the same size as the old one. 
 */
int
gfs_replace_acl(struct inode *inode, struct posix_acl *acl, int access,
		struct gfs_ea_location location)
{
	struct gfs_inode *ip = vn2ip(inode);
	struct gfs_easet_io req;
	int size;
	void *data;
	int error;

	size = posix_acl_to_xattr(acl, NULL, 0);
	GFS_ASSERT(size == GFS_EA_DATA_LEN(location.ea),
		   printk("new acl size = %d, ea size = %u\n", size,
			  GFS_EA_DATA_LEN(location.ea)););

	data = gmalloc(size);

	posix_acl_to_xattr(acl, data, size);

	req.es_data = data;
	req.es_name = (access) ? GFS_POSIX_ACL_ACCESS : GFS_POSIX_ACL_DEFAULT;
	req.es_data_len = size;
	req.es_name_len = (access) ? GFS_POSIX_ACL_ACCESS_LEN : GFS_POSIX_ACL_DEFAULT_LEN;
	req.es_cmd = GFS_EACMD_REPLACE;
	req.es_type = GFS_EATYPE_SYS;

	error = replace_ea(ip->i_sbd, ip, location.ea, &req);
	if (!error)
		gfs_trans_add_bh(ip->i_gl, location.bh);

	kfree(data);

	return error;
}

/**
 * gfs_findacl - returns the requested posix acl
 *
 * this function does not log the inode. It assumes that a lock is already
 * held on it.
 */
int
gfs_findacl(struct gfs_inode *ip, int access, struct posix_acl **acl_ptr,
	    struct gfs_ea_location *location)
{
	struct gfs_sbd *sdp = ip->i_sbd;
	struct posix_acl *acl;
	uint32_t avail_size;
	void *data;
	int error;

	avail_size = sdp->sd_sb.sb_bsize - sizeof(struct gfs_meta_header);
	*acl_ptr = NULL;

	if (!ip->i_di.di_eattr)
		return 0;

	error = find_eattr(ip,
			   (access) ? GFS_POSIX_ACL_ACCESS : GFS_POSIX_ACL_DEFAULT,
			   (access) ? GFS_POSIX_ACL_ACCESS_LEN : GFS_POSIX_ACL_DEFAULT_LEN,
			   GFS_EATYPE_SYS, location);
	if (error <= 0)
		return error;

	data = gmalloc(GFS_EA_DATA_LEN(location->ea));

	error = 0;
	if (GFS_EA_IS_UNSTUFFED(location->ea))
		error = read_unstuffed(data, ip, sdp, location->ea, avail_size,
				       gfs_ea_memcpy);
	else
		gfs_ea_memcpy(data, GFS_EA_DATA(location->ea),
			      GFS_EA_DATA_LEN(location->ea));
	if (error)
		goto out;

	acl = posix_acl_from_xattr(data, GFS_EA_DATA_LEN(location->ea));
	if (IS_ERR(acl))
		error = PTR_ERR(acl);
	else
		*acl_ptr = acl;

 out:
	kfree(data);
	if (error)
		brelse(location->bh);

	return error;
}

int
gfs_getacl(struct inode *inode, int access, struct posix_acl **acl_ptr)
{
	struct gfs_inode *ip = vn2ip(inode);
	struct gfs_sbd *sdp = ip->i_sbd;
	struct gfs_eaget_io req;
	struct posix_acl *acl;
	int size;
	void *data;
	int error = 0;

	*acl_ptr = NULL;

	if (!sdp->sd_args.ar_posixacls)
		return 0;

	req.eg_name = (access) ? GFS_POSIX_ACL_ACCESS : GFS_POSIX_ACL_DEFAULT;
	req.eg_name_len = (access) ? GFS_POSIX_ACL_ACCESS_LEN : GFS_POSIX_ACL_DEFAULT_LEN;
	req.eg_type = GFS_EATYPE_SYS;
	req.eg_len = NULL;
	req.eg_data = NULL;
	req.eg_data_len = 0;

	error = gfs_ea_read_permission(&req, ip);
	if (error)
		return error;

	if (!ip->i_di.di_eattr)
		return error;

	size = get_ea(sdp, ip, &req, gfs_ea_memcpy);
	if (size < 0) {
		if (size != -ENODATA)
			error = size;
		return error;
	}

	data = gmalloc(size);

	req.eg_data = data;
	req.eg_data_len = size;

	size = get_ea(sdp, ip, &req, gfs_ea_memcpy);
	if (size < 0) {
		error = size;
		goto out_free;
	}

	acl = posix_acl_from_xattr(data, size);
	if (IS_ERR(acl))
		error = PTR_ERR(acl);
	else
		*acl_ptr = acl;

 out_free:
	kfree(data);

	return error;
}

int
gfs_setup_new_acl(struct gfs_inode *dip,
		  unsigned int type, unsigned int *mode,
		  struct posix_acl **acl_ptr)
{
	struct gfs_ea_location location;
	struct posix_acl *acl = NULL;
	mode_t access_mode = *mode;
	int error;

	if (type == GFS_FILE_LNK)
		return 0;

	error = gfs_findacl(dip, FALSE, &acl, &location);
	if (error)
		return error;
	if (!acl) {
		(*mode) &= ~current->fs->umask;
		return 0;
	}
	brelse(location.bh);

	if (type == GFS_FILE_DIR) {
		*acl_ptr = acl;
		return 0;
	}

	error = posix_acl_create_masq(acl, &access_mode);
	*mode = access_mode;
	if (error > 0) {
		*acl_ptr = acl;
		return 0;
	}

	posix_acl_release(acl);

	return error;
}

/**
 * gfs_init_default_acl - initializes the default acl
 *
 * NOTE: gfs_init_access_acl must be called first
 */
int
gfs_create_default_acl(struct gfs_inode *dip, struct gfs_inode *ip, void *data,
		       int size)
{
	struct gfs_easet_io req;
	struct gfs_ea_location avail;
	int error;

	memset(&avail, 0, sizeof(struct gfs_ea_location));

	req.es_data = data;
	req.es_name = GFS_POSIX_ACL_DEFAULT;
	req.es_data_len = size;
	req.es_name_len = GFS_POSIX_ACL_DEFAULT_LEN;
	req.es_cmd = GFS_EACMD_CREATE;
	req.es_type = GFS_EATYPE_SYS;

	error = find_sys_space(dip, ip, size, &avail);
	if (error)
		return error;

	avail.ea = prep_ea(avail.ea);

	error = write_ea(ip->i_sbd, dip, ip, avail.ea, &req);
	if (!error)
		gfs_trans_add_bh(ip->i_gl, avail.bh);  /*  Huh!?!  */

	brelse(avail.bh);

	return error;
}

/**
 * gfs_init_access_acl - initialized the access acl
 *
 * NOTE: This must be the first extended attribute that is created for
 *       this inode.
 */
int
gfs_init_access_acl(struct gfs_inode *dip, struct gfs_inode *ip, void *data,
		    int size)
{
	struct gfs_easet_io req;

	req.es_data = data;
	req.es_name = GFS_POSIX_ACL_ACCESS;
	req.es_data_len = size;
	req.es_name_len = GFS_POSIX_ACL_ACCESS_LEN;
	req.es_cmd = GFS_EACMD_CREATE;
	req.es_type = GFS_EATYPE_SYS;

	return init_new_inode_eattr(dip, ip, &req);
}

int
gfs_init_acl(struct gfs_inode *dip, struct gfs_inode *ip, unsigned int type,
	     struct posix_acl *acl)
{
	struct buffer_head *dibh;
	void *data;
	int size;
	int error;

	size = posix_acl_to_xattr(acl, NULL, 0);

	data = gmalloc(size);

	posix_acl_to_xattr(acl, data, size);

	error = gfs_get_inode_buffer(ip, &dibh);
	if (error)
		goto out;

	error = gfs_init_access_acl(dip, ip, data, size);
	if (error)
		goto out_relse;

	if (type == GFS_FILE_DIR) {
		error = gfs_create_default_acl(dip, ip, data, size);
		if (error)
			goto out_relse;
	}

	gfs_trans_add_bh(ip->i_gl, dibh);
	gfs_dinode_out(&ip->i_di, dibh->b_data);

 out_relse:
	brelse(dibh);

 out:
	kfree(data);
	posix_acl_release(acl);

	return error;
}

int
gfs_acl_setattr(struct inode *inode)
{
	struct gfs_inode *ip = vn2ip(inode);
	struct posix_acl *acl;
	struct gfs_ea_location location;
	int error;

	if (S_ISLNK(inode->i_mode))
		return 0;

	memset(&location, 0, sizeof(struct gfs_ea_location));

	error = gfs_findacl(ip, TRUE, &acl, &location); /* Check error here? */
	if (!location.ea)
		return error;

	error = posix_acl_chmod_masq(acl, inode->i_mode);
	if (!error)
		error = gfs_replace_acl(inode, acl, TRUE, location);

	posix_acl_release(acl);
	brelse(location.bh);

	return error;
}
