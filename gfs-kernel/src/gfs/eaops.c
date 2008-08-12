#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <asm/uaccess.h>
#include <linux/xattr.h>
#include <linux/posix_acl.h>

#include "gfs.h"
#include "acl.h"
#include "eaops.h"
#include "eattr.h"

/**
 * gfs_ea_name2type - get the type of the ea, and trucate the type from the name
 * @namep: ea name, possibly with type appended
 *
 * Returns: GFS_EATYPE_XXX
 */

unsigned int
gfs_ea_name2type(const char *name, char **truncated_name)
{
	unsigned int type;

	if (strncmp(name, "system.", 7) == 0) {
		type = GFS_EATYPE_SYS;
		if (truncated_name)
			*truncated_name = strchr(name, '.') + 1;
	} else if (strncmp(name, "user.", 5) == 0) {
		type = GFS_EATYPE_USR;
		if (truncated_name)
			*truncated_name = strchr(name, '.') + 1;
	} else if (strncmp(name, "security.", 9) == 0) {
		type = GFS_EATYPE_SECURITY;
		if (truncated_name)
			*truncated_name = strchr(name, '.') + 1;
	} else {
		type = GFS_EATYPE_UNUSED;
		if (truncated_name)
			*truncated_name = NULL;
	}

	return type;
}

/**
 * system_eo_get -
 * @ip:
 * @er:
 *
 * Returns: errno
 */

static int
system_eo_get(struct gfs_inode *ip, struct gfs_ea_request *er)
{
	if (!GFS_ACL_IS_ACCESS(er->er_name, er->er_name_len) &&
	    !GFS_ACL_IS_DEFAULT(er->er_name, er->er_name_len) &&
	    !capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (ip->i_sbd->sd_args.ar_posix_acls == FALSE &&
	    (GFS_ACL_IS_ACCESS(er->er_name, er->er_name_len) ||
	     GFS_ACL_IS_DEFAULT(er->er_name, er->er_name_len)))
		return -EOPNOTSUPP;

	return gfs_ea_get_i(ip, er);	
}

/**
 * system_eo_set -
 * @ip:
 * @er:
 *
 * Returns: errno
 */

static int
system_eo_set(struct gfs_inode *ip, struct gfs_ea_request *er)
{
	int remove = FALSE;
	int error;

	if (GFS_ACL_IS_ACCESS(er->er_name, er->er_name_len)) {
		er->er_mode = ip->i_vnode->i_mode;
		error = gfs_acl_validate_set(ip, TRUE, er,
					     &remove, &er->er_mode);
		if (error)
			return error;
		error = gfs_ea_set_i(ip, er);
		if (error)
			return error;
		if (remove)
			gfs_ea_remove_i(ip, er);
		return 0;

	} else if (GFS_ACL_IS_DEFAULT(er->er_name, er->er_name_len)) {
		int error = gfs_acl_validate_set(ip, FALSE, er,
						 &remove, NULL);
		if (error)
			return error;
		if (!remove)
			error = gfs_ea_set_i(ip, er);
		else {
			error = gfs_ea_remove_i(ip, er);
			if (error == -ENODATA)
				error = 0;
		}
		return error;
	}

	return -EPERM;
}

/**
 * system_eo_remove -
 * @ip:
 * @er:
 *
 * Returns: errno
 */

static int
system_eo_remove(struct gfs_inode *ip, struct gfs_ea_request *er)
{
	if (GFS_ACL_IS_ACCESS(er->er_name, er->er_name_len)) {
		int error = gfs_acl_validate_remove(ip, TRUE);
		if (error)
			return error;

	} else if (GFS_ACL_IS_DEFAULT(er->er_name, er->er_name_len)) {
		int error = gfs_acl_validate_remove(ip, FALSE);
		if (error)
			return error;

	} else
	        return -EPERM;

	return gfs_ea_remove_i(ip, er);	
}

struct gfs_eattr_operations gfs_user_eaops = {
	.eo_get = gfs_ea_get_i,
	.eo_set = gfs_ea_set_i,
	.eo_remove = gfs_ea_remove_i,
	.eo_name = "user",
};

struct gfs_eattr_operations gfs_system_eaops = {
	.eo_get = system_eo_get,
	.eo_set = system_eo_set,
	.eo_remove = system_eo_remove,
	.eo_name = "system",
};

struct gfs_eattr_operations gfs_security_eaops = {
	.eo_get = gfs_ea_get_i,
	.eo_set = gfs_ea_set_i,
	.eo_remove = gfs_ea_remove_i,
	.eo_name = "security",
};

struct gfs_eattr_operations *gfs_ea_ops[] = {
	NULL,
	&gfs_user_eaops,
	&gfs_system_eaops,
	&gfs_security_eaops,
};

