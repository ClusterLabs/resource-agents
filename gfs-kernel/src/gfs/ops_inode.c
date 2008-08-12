#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/namei.h>
#include <linux/utsname.h>
#include <asm/uaccess.h>
#include <linux/mm.h>
#include <linux/xattr.h>
#include <linux/posix_acl.h>
#include <linux/security.h>

#include "gfs.h"
#include "acl.h"
#include "bmap.h"
#include "dio.h"
#include "dir.h"
#include "eaops.h"
#include "eattr.h"
#include "glock.h"
#include "inode.h"
#include "ops_dentry.h"
#include "ops_inode.h"
#include "page.h"
#include "quota.h"
#include "rgrp.h"
#include "trans.h"
#include "unlinked.h"

/**
 * gfs_security_init -
 * @dip:
 * @ip:
 *
 * Returns: errno
 */

static int
gfs_security_init(struct gfs_inode *dip, struct gfs_inode *ip)
{
	int err;
	size_t len;
	void *value;
	char *name;
	struct gfs_ea_request er;

	err = security_inode_init_security(ip->i_vnode, dip->i_vnode,
					   &name, &value, &len);

	if (err) {
		if (err == -EOPNOTSUPP)
			return 0;
		return err;
	}

	memset(&er, 0, sizeof(struct gfs_ea_request));

	er.er_type = GFS_EATYPE_SECURITY;
	er.er_name = name;
	er.er_data = value;
	er.er_name_len = strlen(name);
	er.er_data_len = len;

	err = gfs_ea_set_i(ip, &er);

	kfree(value);
	kfree(name);

	return err;
}

/**
 * gfs_create - Create a file
 * @dir: The directory in which to create the file
 * @dentry: The dentry of the new file
 * @mode: The mode of the new file
 *
 * Returns: errno
 */

static int
gfs_create(struct inode *dir, struct dentry *dentry,
	   int mode, struct nameidata *nd)
{
	struct gfs_inode *dip = get_v2ip(dir), *ip;
	struct gfs_sbd *sdp = dip->i_sbd;
	struct gfs_holder d_gh, i_gh;
	struct inode *inode;
	int new = TRUE;
	int error;

	atomic_inc(&sdp->sd_ops_inode);

	gfs_unlinked_limit(sdp);

	gfs_holder_init(dip->i_gl, 0, 0, &d_gh);

	for (;;) {
		error = gfs_createi(&d_gh, &dentry->d_name,
				    GFS_FILE_REG, mode,
				    &i_gh);
		if (!error)
			break;
		else if (error != -EEXIST ||
			 (nd && (nd->intent.open.flags & O_EXCL))) {
			gfs_holder_uninit(&d_gh);
			return error;
		}

		error = gfs_lookupi(&d_gh, &dentry->d_name,
				    FALSE, &i_gh);
		if (!error) {
			if (i_gh.gh_gl) {
				new = FALSE;
				break;
			}
		} else {
			gfs_holder_uninit(&d_gh);
			return error;
		}
	}

	ip = get_gl2ip(i_gh.gh_gl);

	if (new) {
		gfs_trans_end(sdp);
		if (dip->i_alloc->al_rgd)
			gfs_inplace_release(dip);
		gfs_quota_unlock_m(dip);
		gfs_unlinked_unlock(sdp, dip->i_alloc->al_ul);
		gfs_alloc_put(dip);
	}

	inode = gfs_iget(ip, CREATE);
	gfs_inode_put(ip);

	if (!inode)
		error = -ENOMEM;
	else
		error = gfs_security_init(dip, ip);

	gfs_glock_dq_uninit(&d_gh);
	gfs_glock_dq_uninit(&i_gh);

	if (error)
		return error;

	d_instantiate(dentry, inode);
	if (new)
		mark_inode_dirty(inode);

	return 0;
}

/**
 * lookup_cdpn_sub_at - Maybe lookup a Context Dependent Pathname
 * @sdp: the filesystem
 * @dentry: the original dentry to lookup
 * @new_dentry: the new dentry, if this was a substitutable path.
 *
 * Returns: the new dentry, a ERR_PTR, or NULL
 */

static struct dentry *
lookup_cdpn_sub_at(struct gfs_sbd *sdp, struct dentry *dentry)
{
	struct dentry *parent, *new = NULL;
	char *buf;

	buf = kmalloc(2 * __NEW_UTS_LEN + 2, GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	parent = dget_parent(dentry);

	if (gfs_filecmp(&dentry->d_name, "@hostname", 9))
		new = lookup_one_len(init_utsname()->nodename,
				     parent,
				     strlen(init_utsname()->nodename));
	else if (gfs_filecmp(&dentry->d_name, "@mach", 5))
		new = lookup_one_len(init_utsname()->machine,
				     parent,
				     strlen(init_utsname()->machine));
	else if (gfs_filecmp(&dentry->d_name, "@os", 3))
		new = lookup_one_len(init_utsname()->sysname,
				     parent,
				     strlen(init_utsname()->sysname));
	else if (gfs_filecmp(&dentry->d_name, "@uid", 4))
		new = lookup_one_len(buf,
				     parent,
				     sprintf(buf, "%u", current->fsuid));
	else if (gfs_filecmp(&dentry->d_name, "@gid", 4))
		new = lookup_one_len(buf,
				     parent,
				     sprintf(buf, "%u", current->fsgid));
	else if (gfs_filecmp(&dentry->d_name, "@sys", 4))
		new = lookup_one_len(buf,
				     parent,
				     sprintf(buf, "%s_%s",
					     init_utsname()->machine,
					     init_utsname()->sysname));
	else if (gfs_filecmp(&dentry->d_name, "@jid", 4))
		new = lookup_one_len(buf,
				     parent,
				     sprintf(buf, "%u",
					     sdp->sd_lockstruct.ls_jid));

	dput(parent);
	kfree(buf);

	return new;
}

/**
 * lookup_cdpn_sub_brace - Maybe lookup a Context Dependent Pathname
 * @sdp: the filesystem
 * @dentry: the original dentry to lookup
 * @new_dentry: the new dentry, if this was a substitutable path.
 *
 * Returns: the new dentry, a ERR_PTR, or NULL
 */

static struct dentry *
lookup_cdpn_sub_brace(struct gfs_sbd *sdp, struct dentry *dentry)
{
	struct dentry *parent, *new = NULL;
	char *buf;

	buf = kmalloc(2 * __NEW_UTS_LEN + 2, GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	parent = dget_parent(dentry);

	if (gfs_filecmp(&dentry->d_name, "{hostname}", 10))
		new = lookup_one_len(init_utsname()->nodename,
				     parent,
				     strlen(init_utsname()->nodename));
	else if (gfs_filecmp(&dentry->d_name, "{mach}", 6))
		new = lookup_one_len(init_utsname()->machine,
				     parent,
				     strlen(init_utsname()->machine));
	else if (gfs_filecmp(&dentry->d_name, "{os}", 4))
		new = lookup_one_len(init_utsname()->sysname,
				     parent,
				     strlen(init_utsname()->sysname));
	else if (gfs_filecmp(&dentry->d_name, "{uid}", 5))
		new = lookup_one_len(buf,
				     parent,
				     sprintf(buf, "%u", current->fsuid));
	else if (gfs_filecmp(&dentry->d_name, "{gid}", 5))
		new = lookup_one_len(buf,
				     parent,
				     sprintf(buf, "%u", current->fsgid));
	else if (gfs_filecmp(&dentry->d_name, "{sys}", 5))
		new = lookup_one_len(buf,
				     parent,
				     sprintf(buf, "%s_%s",
					     init_utsname()->machine,
					     init_utsname()->sysname));
	else if (gfs_filecmp(&dentry->d_name, "{jid}", 5))
		new = lookup_one_len(buf,
				     parent,
				     sprintf(buf, "%u",
					     sdp->sd_lockstruct.ls_jid));

	dput(parent);
	kfree(buf);

	return new;
}

/**
 * gfs_lookup - Look up a filename in a directory and return its inode
 * @dir: The directory inode
 * @dentry: The dentry of the new inode
 * @nd: passed from Linux VFS, ignored by us
 *
 * Called by the VFS layer. Lock dir and call gfs_lookupi()
 *
 * Returns: errno
 */

static struct dentry *
gfs_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nd)
{
	struct gfs_inode *dip = get_v2ip(dir), *ip;
	struct gfs_holder d_gh, i_gh;
	struct inode *inode = NULL;
	int error;

	atomic_inc(&dip->i_sbd->sd_ops_inode);

	/*  Do Context Dependent Path Name expansion  */

	if (*dentry->d_name.name == '@' && dentry->d_name.len > 1) {
		struct dentry *new_dentry;
		new_dentry = lookup_cdpn_sub_at(dip->i_sbd, dentry);
		if (new_dentry)
			return new_dentry;
	} else if (*dentry->d_name.name == '{' && dentry->d_name.len > 2) {
		struct dentry *new_dentry;
		new_dentry = lookup_cdpn_sub_brace(dip->i_sbd, dentry);
		if (new_dentry)
			return new_dentry;
	}

	dentry->d_op = &gfs_dops;

	gfs_holder_init(dip->i_gl, 0, 0, &d_gh);

	error = gfs_lookupi(&d_gh, &dentry->d_name, FALSE, &i_gh);
	if (error) {
		gfs_holder_uninit(&d_gh);
		return ERR_PTR(error);
	}

	if (i_gh.gh_gl) {
		ip = get_gl2ip(i_gh.gh_gl);

		inode = gfs_iget(ip, CREATE);
		gfs_inode_put(ip);

		gfs_glock_dq_uninit(&d_gh);
		gfs_glock_dq_uninit(&i_gh);

		if (!inode)
			return ERR_PTR(-ENOMEM);
	} else
		gfs_holder_uninit(&d_gh);

	if (inode)
		return d_splice_alias(inode, dentry);
	d_add(dentry, inode);

	return NULL;
}

/**
 * gfs_link - Link to a file
 * @old_dentry: The inode to link
 * @dir: Add link to this directory
 * @dentry: The name of the link
 *
 * Link the inode in "old_dentry" into the directory "dir" with the
 * name in "dentry".
 *
 * Returns: errno
 */

static int
gfs_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry)
{
	struct gfs_inode *dip = get_v2ip(dir);
	struct gfs_sbd *sdp = dip->i_sbd;
	struct inode *inode = old_dentry->d_inode;
	struct gfs_inode *ip = get_v2ip(inode);
	struct gfs_alloc *al = NULL;
	struct gfs_holder ghs[2];
	int alloc_required;
	int error;

	atomic_inc(&sdp->sd_ops_inode);

	if (ip->i_di.di_type == GFS_FILE_DIR)
		return -EPERM;

	gfs_holder_init(dip->i_gl, LM_ST_EXCLUSIVE, 0, &ghs[0]);
	gfs_holder_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &ghs[1]);

	error = gfs_glock_nq_m(2, ghs);
	if (error)
		goto fail;

	error = inode_permission(dir, MAY_WRITE | MAY_EXEC);
	if (error)
		goto fail_gunlock;

	error = gfs_dir_search(dip, &dentry->d_name, NULL, NULL);
	switch (error) {
	case -ENOENT:
		break;
	case 0:
		error = -EEXIST;
	default:
		goto fail_gunlock;
	}

	if (!dip->i_di.di_nlink) {
		error = -EINVAL;
		goto fail_gunlock;
	}
	if (dip->i_di.di_entries == (uint32_t)-1) {
		error = -EFBIG;
		goto fail_gunlock;
	}
	if (IS_IMMUTABLE(inode) || IS_APPEND(inode)) {
		error = -EPERM;
		goto fail_gunlock;
	}
	if (!ip->i_di.di_nlink) {
		error = -EINVAL;
		goto fail_gunlock;
	}
	if (ip->i_di.di_nlink == (uint32_t)-1) {
		error = -EMLINK;
		goto fail_gunlock;
	}

	error = gfs_diradd_alloc_required(dip, &dentry->d_name, &alloc_required);
	if (error)
		goto fail_gunlock;

	if (alloc_required) {
		al = gfs_alloc_get(dip);

		error = gfs_quota_lock_m(dip, NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
		if (error)
			goto fail_alloc;

		error = gfs_quota_check(dip, dip->i_di.di_uid, dip->i_di.di_gid);
		if (error)
			goto fail_gunlock_q;

		al->al_requested_meta = sdp->sd_max_dirres;

		error = gfs_inplace_reserve(dip);
		if (error)
			goto fail_gunlock_q;

		/* Trans may require:
		   two dinode blocks, directory modifications to add an entry,
		   RG bitmap blocks to allocate from, and quota change */

		error = gfs_trans_begin(sdp,
					2 + sdp->sd_max_dirres +
					al->al_rgd->rd_ri.ri_length,
					1);
		if (error)
			goto fail_ipres;
	} else {
		/*  Trans may require:
		    Two dinode blocks and a leaf block.  */

		error = gfs_trans_begin(sdp, 3, 0);
		if (error)
			goto fail_ipres;
	}

	error = gfs_dir_add(dip, &dentry->d_name, &ip->i_num, ip->i_di.di_type);
	if (error)
		goto fail_end_trans;

	error = gfs_change_nlink(ip, +1);
	if (error)
		goto fail_end_trans;

	gfs_trans_end(sdp);

	if (alloc_required) {
		gfs_assert_warn(sdp, al->al_alloced_meta);
		gfs_inplace_release(dip);
		gfs_quota_unlock_m(dip);
		gfs_alloc_put(dip);
	}

	gfs_glock_dq_m(2, ghs);

	gfs_holder_uninit(&ghs[0]);
	gfs_holder_uninit(&ghs[1]);

	atomic_inc(&inode->i_count);

	d_instantiate(dentry, inode);
	mark_inode_dirty(inode);

	return 0;

 fail_end_trans:
	gfs_trans_end(sdp);

 fail_ipres:
	if (alloc_required)
		gfs_inplace_release(dip);

 fail_gunlock_q:
	if (alloc_required)
		gfs_quota_unlock_m(dip);

 fail_alloc:
	if (alloc_required)
		gfs_alloc_put(dip);

 fail_gunlock:
	gfs_glock_dq_m(2, ghs);

 fail:
	gfs_holder_uninit(&ghs[0]);
	gfs_holder_uninit(&ghs[1]);

	return error;
}

/**
 * gfs_unlink - Unlink a file
 * @dir: The inode of the directory containing the file to unlink
 * @dentry: The file itself
 *
 * Unlink a file.  Call gfs_unlinki()
 *
 * Returns: errno
 */

static int
gfs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct gfs_inode *dip = get_v2ip(dir);
	struct gfs_sbd *sdp = dip->i_sbd;
	struct gfs_inode *ip = get_v2ip(dentry->d_inode);
	struct gfs_holder ghs[2];
	int error;

	atomic_inc(&sdp->sd_ops_inode);

	gfs_unlinked_limit(sdp);

	gfs_holder_init(dip->i_gl, LM_ST_EXCLUSIVE, 0, &ghs[0]);
	gfs_holder_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &ghs[1]);

	error = gfs_glock_nq_m(2, ghs);
	if (error)
		goto fail;

	error = gfs_unlink_ok(dip, &dentry->d_name, ip);
	if (error)
		goto fail_gunlock;

	/*  Trans may require:
	    Two dinode blocks and one modified directory leaf block
	    and one unlinked tag.  */

	error = gfs_trans_begin(sdp, 3, 1);
	if (error)
		goto fail_gunlock;

	error = gfs_unlinki(dip, &dentry->d_name, ip);
	if (error)
		goto fail_end_trans;

	gfs_trans_end(sdp);

	gfs_glock_dq_m(2, ghs);

	gfs_holder_uninit(&ghs[0]);
	gfs_holder_uninit(&ghs[1]);

	return 0;

 fail_end_trans:
	gfs_trans_end(sdp);

 fail_gunlock:
	gfs_glock_dq_m(2, ghs);

 fail:
	gfs_holder_uninit(&ghs[0]);
	gfs_holder_uninit(&ghs[1]);

	return error;
}

/**
 * gfs_symlink - Create a symlink
 * @dir: The directory to create the symlink in
 * @dentry: The dentry to put the symlink in
 * @symname: The thing which the link points to
 *
 * Returns: errno
 */

static int
gfs_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
{
	struct gfs_inode *dip = get_v2ip(dir), *ip;
	struct gfs_sbd *sdp = dip->i_sbd;
	struct gfs_holder d_gh, i_gh;
	struct inode *inode;
	struct buffer_head *dibh;
	int size;
	int error;

	atomic_inc(&sdp->sd_ops_inode);

	gfs_unlinked_limit(sdp);

	/* Must be stuffed with a null terminator for gfs_follow_link() */
	size = strlen(symname);
	if (size > sdp->sd_sb.sb_bsize - sizeof(struct gfs_dinode) - 1)
	        return -ENAMETOOLONG;

	gfs_holder_init(dip->i_gl, 0, 0, &d_gh);

	error = gfs_createi(&d_gh, &dentry->d_name,
			    GFS_FILE_LNK, S_IFLNK | S_IRWXUGO,
			    &i_gh);
	if (error) {
		gfs_holder_uninit(&d_gh);
		return error;
	}

	ip = get_gl2ip(i_gh.gh_gl);

	ip->i_di.di_size = size;

	error = gfs_get_inode_buffer(ip, &dibh);

	if (!gfs_assert_withdraw(sdp, !error)) {
		gfs_dinode_out(&ip->i_di, dibh->b_data);
		memcpy(dibh->b_data + sizeof(struct gfs_dinode), symname, size);
		brelse(dibh);
	}

	gfs_trans_end(sdp);
	if (dip->i_alloc->al_rgd)
		gfs_inplace_release(dip);
	gfs_quota_unlock_m(dip);
	gfs_unlinked_unlock(sdp, dip->i_alloc->al_ul);
	gfs_alloc_put(dip);

	inode = gfs_iget(ip, CREATE);
	gfs_inode_put(ip);

	error = gfs_security_init(dip, ip);

	gfs_glock_dq_uninit(&d_gh);
	gfs_glock_dq_uninit(&i_gh);

	if (error)
		return error;

	if (!inode)
		return -ENOMEM;

	d_instantiate(dentry, inode);
	mark_inode_dirty(inode);

	return 0;
}

/**
 * gfs_mkdir - Make a directory
 * @dir: The parent directory of the new one
 * @dentry: The dentry of the new directory
 * @mode: The mode of the new directory
 *
 * Returns: errno
 */

static int
gfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	struct gfs_inode *dip = get_v2ip(dir), *ip;
	struct gfs_sbd *sdp = dip->i_sbd;
	struct gfs_holder d_gh, i_gh;
	struct inode *inode;
	struct buffer_head *dibh;
	int error;

	atomic_inc(&sdp->sd_ops_inode);

	gfs_unlinked_limit(sdp);

	gfs_holder_init(dip->i_gl, 0, 0, &d_gh);

	error = gfs_createi(&d_gh, &dentry->d_name,
			    GFS_FILE_DIR, S_IFDIR | mode,
			    &i_gh);
	if (error) {
		gfs_holder_uninit(&d_gh);
		return error;
	}

	ip = get_gl2ip(i_gh.gh_gl);

	ip->i_di.di_nlink = 2;
	ip->i_di.di_size = sdp->sd_sb.sb_bsize - sizeof(struct gfs_dinode);
	ip->i_di.di_flags |= GFS_DIF_JDATA;
	ip->i_di.di_payload_format = GFS_FORMAT_DE;
	ip->i_di.di_entries = 2;

	error = gfs_get_inode_buffer(ip, &dibh);

	if (!gfs_assert_withdraw(sdp, !error)) {
		struct gfs_dinode *di = (struct gfs_dinode *)dibh->b_data;
		struct gfs_dirent *dent;

		gfs_dirent_alloc(ip, dibh, 1, &dent);

		dent->de_inum = di->di_num; /* already GFS endian */
		dent->de_hash = gfs_dir_hash(".", 1);
		dent->de_hash = cpu_to_gfs32(dent->de_hash);
		dent->de_type = cpu_to_gfs16(GFS_FILE_DIR);
		memcpy((char *) (dent + 1), ".", 1);
		di->di_entries = cpu_to_gfs32(1);

		gfs_dirent_alloc(ip, dibh, 2, &dent);

		gfs_inum_out(&dip->i_num, (char *) &dent->de_inum);
		dent->de_hash = gfs_dir_hash("..", 2);
		dent->de_hash = cpu_to_gfs32(dent->de_hash);
		dent->de_type = cpu_to_gfs16(GFS_FILE_DIR);
		memcpy((char *) (dent + 1), "..", 2);

		gfs_dinode_out(&ip->i_di, (char *)di);

		brelse(dibh);
	}

	error = gfs_change_nlink(dip, +1);
	gfs_assert_withdraw(sdp, !error); /* dip already pinned */

	gfs_trans_end(sdp);
	if (dip->i_alloc->al_rgd)
		gfs_inplace_release(dip);
	gfs_quota_unlock_m(dip);
	gfs_unlinked_unlock(sdp, dip->i_alloc->al_ul);
	gfs_alloc_put(dip);

	inode = gfs_iget(ip, CREATE);
	gfs_inode_put(ip);

	if (!inode)
		return -ENOMEM;

	error = gfs_security_init(dip, ip);

	gfs_glock_dq_uninit(&d_gh);
	gfs_glock_dq_uninit(&i_gh);

	if (error)
		return error;

	d_instantiate(dentry, inode);
	mark_inode_dirty(inode);

	return 0;
}

/**
 * gfs_rmdir - Remove a directory
 * @dir: The parent directory of the directory to be removed
 * @dentry: The dentry of the directory to remove
 *
 * Remove a directory. Call gfs_rmdiri()
 *
 * Returns: errno
 */

static int
gfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct gfs_inode *dip = get_v2ip(dir);
	struct gfs_sbd *sdp = dip->i_sbd;
	struct gfs_inode *ip = get_v2ip(dentry->d_inode);
	struct gfs_holder ghs[2];
	int error;

	atomic_inc(&sdp->sd_ops_inode);

	gfs_unlinked_limit(sdp);

	gfs_holder_init(dip->i_gl, LM_ST_EXCLUSIVE, 0, &ghs[0]);
	gfs_holder_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &ghs[1]);

	error = gfs_glock_nq_m(2, ghs);
	if (error)
		goto fail;

	error = gfs_unlink_ok(dip, &dentry->d_name, ip);
	if (error)
		goto fail_gunlock;

	if (ip->i_di.di_entries < 2) {
		if (gfs_consist_inode(ip))
			gfs_dinode_print(&ip->i_di);
		error = -EIO;
		goto fail_gunlock;
	}
	if (ip->i_di.di_entries > 2) {
		error = -ENOTEMPTY;
		goto fail_gunlock;
	}

	/* Trans may require:
	   Two dinode blocks, one directory leaf block containing the
	   entry to be rmdired, two leaf blocks containing . and .. of
	   the directory being rmdired, and one unlinked tag */

	error = gfs_trans_begin(sdp, 5, 1);
	if (error)
		goto fail_gunlock;

	error = gfs_rmdiri(dip, &dentry->d_name, ip);
	if (error)
		goto fail_end_trans;

	gfs_trans_end(sdp);

	gfs_glock_dq_m(2, ghs);

	gfs_holder_uninit(&ghs[0]);
	gfs_holder_uninit(&ghs[1]);

	return 0;

 fail_end_trans:
	gfs_trans_end(sdp);

 fail_gunlock:
	gfs_glock_dq_m(2, ghs);

 fail:
	gfs_holder_uninit(&ghs[0]);
	gfs_holder_uninit(&ghs[1]);

	return error;
}

/**
 * gfs_mknod - Make a special file
 * @dir: The directory in which the special file will reside
 * @dentry: The dentry of the special file
 * @mode: The mode of the special file
 * @rdev: The device specification of the special file
 *
 */

static int
gfs_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t dev)
{
	struct gfs_inode *dip = get_v2ip(dir), *ip;
	struct gfs_sbd *sdp = dip->i_sbd;
	struct gfs_holder d_gh, i_gh;
	struct inode *inode;
	struct buffer_head *dibh;
	uint16_t type = 0;
	uint32_t major = 0, minor = 0;
	int error;

	atomic_inc(&sdp->sd_ops_inode);

	gfs_unlinked_limit(sdp);

	switch (mode & S_IFMT) {
	case S_IFBLK:
		type = GFS_FILE_BLK;
		major = MAJOR(dev);
		minor = MINOR(dev);
		break;
	case S_IFCHR:
		type = GFS_FILE_CHR;
		major = MAJOR(dev);
		minor = MINOR(dev);
		break;
	case S_IFIFO:
		type = GFS_FILE_FIFO;
		break;
	case S_IFSOCK:
		type = GFS_FILE_SOCK;
		break;
	default:
		printk("GFS: fsid=%s: mknod() with invalid type (%d)\n",
		       sdp->sd_fsname, mode);
		return -EINVAL;
	};

	gfs_holder_init(dip->i_gl, 0, 0, &d_gh);

	error = gfs_createi(&d_gh, &dentry->d_name,
			    type, mode,
			    &i_gh);
	if (error) {
		gfs_holder_uninit(&d_gh);
		return error;
	}

	ip = get_gl2ip(i_gh.gh_gl);

	ip->i_di.di_major = major;
	ip->i_di.di_minor = minor;

	error = gfs_get_inode_buffer(ip, &dibh);

	if (!gfs_assert_withdraw(sdp, !error)) {
		gfs_dinode_out(&ip->i_di, dibh->b_data);
		brelse(dibh);
	}

	gfs_trans_end(sdp);
	if (dip->i_alloc->al_rgd)
		gfs_inplace_release(dip);
	gfs_quota_unlock_m(dip);
	gfs_unlinked_unlock(sdp, dip->i_alloc->al_ul);
	gfs_alloc_put(dip);

	inode = gfs_iget(ip, CREATE);
	gfs_inode_put(ip);

	error = gfs_security_init(dip, ip);

	gfs_glock_dq_uninit(&d_gh);
	gfs_glock_dq_uninit(&i_gh);

	if (error)
		return error;

	if (!inode)
		return -ENOMEM;

	d_instantiate(dentry, inode);
	mark_inode_dirty(inode);

	return 0;
}

/**
 * gfs_rename - Rename a file
 * @odir: Parent directory of old file name
 * @odentry: The old dentry of the file
 * @ndir: Parent directory of new file name
 * @ndentry: The new dentry of the file
 *
 * Returns: errno
 */

static int
gfs_rename(struct inode *odir, struct dentry *odentry,
	   struct inode *ndir, struct dentry *ndentry)
{
	struct gfs_inode *odip = get_v2ip(odir);
	struct gfs_inode *ndip = get_v2ip(ndir);
	struct gfs_inode *ip = get_v2ip(odentry->d_inode);
	struct gfs_inode *nip = NULL;
	struct gfs_sbd *sdp = odip->i_sbd;
	struct qstr name;
	struct gfs_alloc *al;
	struct gfs_holder ghs[4], r_gh;
	unsigned int num_gh;
	int dir_rename = FALSE;
	int alloc_required;
	unsigned int x;
	int error;

	atomic_inc(&sdp->sd_ops_inode);

	gfs_unlinked_limit(sdp);

	if (ndentry->d_inode) {
		nip = get_v2ip(ndentry->d_inode);
		if (ip == nip)
			return 0;
	}

	/*  Make sure we aren't trying to move a dirctory into it's subdir  */

	if (ip->i_di.di_type == GFS_FILE_DIR && odip != ndip) {
		dir_rename = TRUE;

		error = gfs_glock_nq_init(sdp->sd_rename_gl,
					  LM_ST_EXCLUSIVE, 0,
					  &r_gh);
		if (error)
			return error;

		error = gfs_ok_to_move(ip, ndip);
		if (error)
			goto fail;
	}

	gfs_holder_init(odip->i_gl, LM_ST_EXCLUSIVE, 0, &ghs[0]);
	gfs_holder_init(ndip->i_gl, LM_ST_EXCLUSIVE, 0, &ghs[1]);
	num_gh = 2;

	if (nip)
		gfs_holder_init(nip->i_gl, LM_ST_EXCLUSIVE, 0, &ghs[num_gh++]);

	if (dir_rename)
		gfs_holder_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &ghs[num_gh++]);

	error = gfs_glock_nq_m(num_gh, ghs);
	if (error)
		goto fail_uninit;

	/*  Check out the old directory  */

	error = gfs_unlink_ok(odip, &odentry->d_name, ip);
	if (error)
		goto fail_gunlock;

	/*  Check out the new directory  */

	if (nip) {
		error = gfs_unlink_ok(ndip, &ndentry->d_name, nip);
		if (error)
			goto fail_gunlock;

		if (nip->i_di.di_type == GFS_FILE_DIR) {
			if (nip->i_di.di_entries < 2) {
				if (gfs_consist_inode(nip))
					gfs_dinode_print(&nip->i_di);
				error = -EIO;
				goto fail_gunlock;
			}
			if (nip->i_di.di_entries > 2) {
				error = -ENOTEMPTY;
				goto fail_gunlock;
			}
		}
	} else {
		error = inode_permission(ndir, MAY_WRITE | MAY_EXEC);
		if (error)
			goto fail_gunlock;

		error = gfs_dir_search(ndip, &ndentry->d_name, NULL, NULL);
		switch (error) {
		case -ENOENT:
			error = 0;
			break;
		case 0:
			error = -EEXIST;
		default:
			goto fail_gunlock;
		};

		if (odip != ndip) {
			if (!ndip->i_di.di_nlink) {
				error = -EINVAL;
				goto fail_gunlock;
			}
			if (ndip->i_di.di_entries == (uint32_t)-1) {
				error = -EFBIG;
				goto fail_gunlock;
			}
			if (ip->i_di.di_type == GFS_FILE_DIR &&
			    ndip->i_di.di_nlink == (uint32_t)-1) {
				error = -EMLINK;
				goto fail_gunlock;
			}
		}
	}

	error = gfs_diradd_alloc_required(ndip, &ndentry->d_name, &alloc_required);
	if (error)
		goto fail_gunlock;

	if (alloc_required) {
		al = gfs_alloc_get(ndip);

		error = gfs_quota_lock_m(ndip,
					    NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
		if (error)
			goto fail_alloc;

		error = gfs_quota_check(ndip, ndip->i_di.di_uid, ndip->i_di.di_gid);
		if (error)
			goto fail_gunlock_q;

		al->al_requested_meta = sdp->sd_max_dirres;

		error = gfs_inplace_reserve(ndip);
		if (error)
			goto fail_gunlock_q;

		/* Trans may require:
		   Dinodes for the srcdir, srcino, dstdir, dstino.  Blocks for
		   adding the entry to dstdir.  RG bitmaps for that allocation.
		   One leaf block in the srcdir for removal of the entry.
		   One leaf block for changing .. in srcino (if it's a directory).
		   Two leaf blocks for removing . and .. from dstino (if it exists
		   and it's a directory), one unlinked tag, and one quota block. */

		error = gfs_trans_begin(sdp,
					8 + sdp->sd_max_dirres +
					al->al_rgd->rd_ri.ri_length,
					2);
		if (error)
			goto fail_ipres;
	} else {
		/* Trans may require:
		   Dinodes for the srcdir, srcino, dstdir, dstino.  One block for
		   adding the entry to dstdir.
		   One leaf block in the srcdir for removal of the entry.
		   One leaf block for changing .. in srcino (if it's a directory).
		   Two leaf blocks for removing . and .. from dstino (if it exists
		   and it's a directory), and one unlinked tag. */

		error = gfs_trans_begin(sdp, 9, 1);
		if (error)
			goto fail_ipres;
	}

	/*  Remove the target file, if it exists  */

	if (nip) {
		if (nip->i_di.di_type == GFS_FILE_DIR)
			error = gfs_rmdiri(ndip, &ndentry->d_name, nip);
		else
			error = gfs_unlinki(ndip, &ndentry->d_name, nip);

		if (error)
			goto fail_end_trans;
	}

	if (dir_rename) {
		error = gfs_change_nlink(ndip, +1);
		if (error)
			goto fail_end_trans;
		error = gfs_change_nlink(odip, -1);
		if (error)
			goto fail_end_trans;

		name.len = 2;
		name.name = "..";

		error = gfs_dir_mvino(ip, &name, &ndip->i_num, GFS_FILE_DIR);
		if (error)
			goto fail_end_trans;
	}

	error = gfs_dir_del(odip, &odentry->d_name);
	if (error)
		goto fail_end_trans;

	error = gfs_dir_add(ndip, &ndentry->d_name, &ip->i_num, ip->i_di.di_type);
	if (error)
		goto fail_end_trans;

	if (dir_rename)
		gfs_trans_add_gl(sdp->sd_rename_gl);

	gfs_trans_end(sdp);

	if (alloc_required) {
		/*  Don't check al->al_alloced_meta and friends.  */
		gfs_inplace_release(ndip);
		gfs_quota_unlock_m(ndip);
		gfs_alloc_put(ndip);
	}

	gfs_glock_dq_m(num_gh, ghs);

	for (x = 0; x < num_gh; x++)
		gfs_holder_uninit(&ghs[x]);

	if (dir_rename)
		gfs_glock_dq_uninit(&r_gh);

	return 0;

 fail_end_trans:
	gfs_trans_end(sdp);

 fail_ipres:
	if (alloc_required)
		gfs_inplace_release(ndip);

 fail_gunlock_q:
	if (alloc_required)
		gfs_quota_unlock_m(ndip);

 fail_alloc:
	if (alloc_required)
		gfs_alloc_put(ndip);

 fail_gunlock:
	gfs_glock_dq_m(num_gh, ghs);

 fail_uninit:
	for (x = 0; x < num_gh; x++)
		gfs_holder_uninit(&ghs[x]);

 fail:
	if (dir_rename)
		gfs_glock_dq_uninit(&r_gh);

	return error;
}

/**
 * gfs_readlink - Read the value of a symlink
 * @dentry: the symlink
 * @buf: the buffer to read the symlink data into
 * @size: the size of the buffer
 *
 * Returns: errno
 */

static int
gfs_readlink(struct dentry *dentry, char *user_buf, int user_size)
{
	struct gfs_inode *ip = get_v2ip(dentry->d_inode);
	char array[GFS_FAST_NAME_SIZE], *buf = array;
	unsigned int len = GFS_FAST_NAME_SIZE;
	int error;

	atomic_inc(&ip->i_sbd->sd_ops_inode);

	error = gfs_readlinki(ip, &buf, &len);
	if (error)
		return error;

	if (user_size > len - 1)
		user_size = len - 1;

	if (copy_to_user(user_buf, buf, user_size))
		error = -EFAULT;
	else
		error = user_size;

	if (buf != array)
		kfree(buf);

	return error;
}

/**
 * gfs_follow_link - Follow a symbolic link
 * @dentry: The dentry of the link
 * @nd: Data that we pass to vfs_follow_link()
 *
 * This can handle symlinks of any size. It is optimised for symlinks
 * under GFS_FAST_NAME_SIZE.
 *
 * Returns: 0 on success or error code
 */

static void *
gfs_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	struct gfs_inode *ip = get_v2ip(dentry->d_inode);
	char array[GFS_FAST_NAME_SIZE], *buf = array;
	unsigned int len = GFS_FAST_NAME_SIZE;
	int error;

	atomic_inc(&ip->i_sbd->sd_ops_inode);

	error = gfs_readlinki(ip, &buf, &len);
	if (!error) {
		error = vfs_follow_link(nd, buf);
		if (buf != array)
			kfree(buf);
	}

	return ERR_PTR(error);
}

/**
 * gfs_permission_i -
 * @inode:
 * @mask:
 *
 * Shamelessly ripped from ext3
 *
 * Returns: errno
 */

static int
gfs_permission_i(struct inode *inode, int mask)
{
	return generic_permission(inode, mask, gfs_check_acl);
}

/**
 * gfs_permission -
 * @inode:
 * @mask:
 *
 * Returns: errno
 */

static int
gfs_permission(struct inode *inode, int mask)
{
	struct gfs_inode *ip = get_v2ip(inode);
	struct gfs_holder i_gh;
	int error;

	atomic_inc(&ip->i_sbd->sd_ops_inode);

	error = gfs_glock_nq_init(ip->i_gl,
				  LM_ST_SHARED, LM_FLAG_ANY,
				  &i_gh);
	if (error)
		return error;

	error = gfs_permission_i(inode, mask);

	gfs_glock_dq_uninit(&i_gh);

	return error;
}

/**
 * gfs_setattr - Change attributes on an inode
 * @dentry: The dentry which is changing
 * @attr: The structure describing the change
 *
 * The VFS layer wants to change one or more of an inodes attributes.  Write
 * that change out to disk.
 *
 * Returns: errno
 */

static int
gfs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	struct gfs_inode *ip = get_v2ip(inode);
	struct gfs_sbd *sdp = ip->i_sbd;
	struct gfs_holder i_gh;
	int error;

	atomic_inc(&sdp->sd_ops_inode);

	/* Bugzilla 203170: we'll have the same deadlock as described 
	 * in bugzilla 173912 if
	 * 1. without RHEL4's DIO_CLUSTER_LOCKING, and
	 * 2. we come down to this line of code from do_truncate()
	 *    where i_sem(i_mutex) and i_alloc_sem have been taken, and
	 * 3. grab the exclusive glock here.
	 * To avoid this to happen, i_alloc_sem must be dropped and trust
	 * be put into glock that it can carry the same protection. 
	 *
	 * One issue with dropping i_alloc_sem is that the gfs_setattr() 
	 * can be invoked from other code path without this sempaphore. 
	 * We'll need a new rwsem function that can "up" the semaphore 
	 * only when it is needed. Before that happens (will research the 
	 * possibility), i_alloc_sem (now) is a meaningless lock within 
	 * GFS. If it is ever been used by other non-directIO code, this
	 * hack will fall apart.
	 *
	 * wcheng@redhat.com 10/14/06  
	 */ 
	if (attr->ia_valid & ATTR_SIZE) {
		up_write(&dentry->d_inode->i_alloc_sem); 
	}
	
	error = gfs_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &i_gh);

	if (attr->ia_valid & ATTR_SIZE) {
		down_write(&dentry->d_inode->i_alloc_sem); 
	}
	
	if (error)
		return error;

	if (IS_IMMUTABLE(inode) || IS_APPEND(inode)) {
		error = -EPERM;
		goto fail;
	}

	error = inode_change_ok(inode, attr);
	if (error)
		goto fail;

	if (attr->ia_valid & ATTR_SIZE) {
		error = inode_permission(inode, MAY_WRITE);
		if (error)
			goto fail;

		if (attr->ia_size != ip->i_di.di_size) {
			error = vmtruncate(inode, attr->ia_size);
			if (error)
				goto fail;
		}

		error = gfs_truncatei(ip, attr->ia_size, gfs_truncator_page);
		if (error) {
			if (inode->i_size != ip->i_di.di_size)
				i_size_write(inode, ip->i_di.di_size);
			goto fail;
		}

		if ((sdp->sd_vfs->s_flags & MS_SYNCHRONOUS) &&
		    !gfs_is_jdata(ip))
			i_gh.gh_flags |= GL_SYNC;
	}

	else if (attr->ia_valid & (ATTR_UID | ATTR_GID)) {
		struct gfs_alloc *al;
		struct buffer_head *dibh;
		uint32_t ouid, ogid, nuid, ngid;

		ouid = ip->i_di.di_uid;
		ogid = ip->i_di.di_gid;
		nuid = attr->ia_uid;
		ngid = attr->ia_gid;

		if (!(attr->ia_valid & ATTR_UID) || ouid == nuid)
			ouid = nuid = NO_QUOTA_CHANGE;
		if (!(attr->ia_valid & ATTR_GID) || ogid == ngid)
			ogid = ngid = NO_QUOTA_CHANGE;

		al = gfs_alloc_get(ip);

		error = gfs_quota_lock_m(ip, nuid, ngid);
		if (error)
			goto fail_alloc;

		if (ouid != NO_QUOTA_CHANGE || ogid != NO_QUOTA_CHANGE) {
			error = gfs_quota_check(ip, nuid, ngid);
			if (error)
				goto fail_gunlock_q;
		}

		/* Trans may require:
		   one dinode block and one quota change block */

		error = gfs_trans_begin(sdp, 1, 1);
		if (error)
			goto fail_gunlock_q;

		error = gfs_get_inode_buffer(ip, &dibh);
		if (error)
			goto fail_end_trans;

		if (ouid != NO_QUOTA_CHANGE || ogid != NO_QUOTA_CHANGE) {
			gfs_trans_add_quota(sdp, -ip->i_di.di_blocks,
					    ouid, ogid);
			gfs_trans_add_quota(sdp, ip->i_di.di_blocks,
					    nuid, ngid);
		}

		error = inode_setattr(inode, attr);
		gfs_assert_warn(sdp, !error);
		gfs_inode_attr_out(ip);

		gfs_trans_add_bh(ip->i_gl, dibh);
		gfs_dinode_out(&ip->i_di, dibh->b_data);
		brelse(dibh);

		gfs_trans_end(sdp);

		gfs_quota_unlock_m(ip);
		gfs_alloc_put(ip);
	}

	else if ((attr->ia_valid & ATTR_MODE) && IS_POSIXACL(inode)) {
		error = gfs_acl_chmod(ip, attr);
		if (error)
			goto fail;
	}

	else {
		error = gfs_setattr_simple(ip, attr);
		if (error)
			goto fail;
	}

	gfs_glock_dq_uninit(&i_gh);

	mark_inode_dirty(inode);

	return error;

 fail_end_trans:
	gfs_trans_end(sdp);

 fail_gunlock_q:
	gfs_quota_unlock_m(ip);

 fail_alloc:
	gfs_alloc_put(ip);

 fail:
	gfs_glock_dq_uninit(&i_gh);

	return error;
}

/**
 * gfs_getattr - Read out an inode's attributes
 * @mnt: ?
 * @dentry: The dentry to stat
 * @stat: The inode's stats
 *
 * Returns: errno
 */

static int
gfs_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
	struct gfs_inode *ip = get_v2ip(inode);
	struct gfs_holder gh;
	int error;

	atomic_inc(&ip->i_sbd->sd_ops_inode);

	error = gfs_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_ANY, &gh);
	if (!error) {
		generic_fillattr(inode, stat);
		gfs_glock_dq_uninit(&gh);
	}

	return error;
}

/**
 * gfs_setxattr - Set (or create or replace) an inode's extended attribute
 * @dentry: 
 * @name: 
 * @data: 
 * @size: 
 * @flags: 
 *
 * Returns: errno
 */

int
gfs_setxattr(struct dentry *dentry, const char *name,
	     const void *data, size_t size,
	     int flags)
{
	struct gfs_ea_request er;

	atomic_inc(&get_v2sdp(dentry->d_inode->i_sb)->sd_ops_inode);

	memset(&er, 0, sizeof(struct gfs_ea_request));
	er.er_type = gfs_ea_name2type(name, &er.er_name);
	if (er.er_type == GFS_EATYPE_UNUSED)
	        return -EOPNOTSUPP;
	er.er_data = (char *)data;
	er.er_name_len = strlen(er.er_name);
	er.er_data_len = size;
	er.er_flags = flags;

	return gfs_ea_set(get_v2ip(dentry->d_inode), &er);
}

/**
 * gfs_getxattr -
 * @dentry:
 * @name:
 * @data:
 * @size:
 *
 * Returns: The number of bytes put into data, or -errno
 */

ssize_t
gfs_getxattr(struct dentry *dentry, const char *name,
	     void *data, size_t size)
{
	struct gfs_ea_request er;

	atomic_inc(&get_v2sdp(dentry->d_inode->i_sb)->sd_ops_inode);

	memset(&er, 0, sizeof(struct gfs_ea_request));
	er.er_type = gfs_ea_name2type(name, &er.er_name);
	if (er.er_type == GFS_EATYPE_UNUSED)
	        return -EOPNOTSUPP;
	er.er_data = data;
	er.er_name_len = strlen(er.er_name);
	er.er_data_len = size;

	return gfs_ea_get(get_v2ip(dentry->d_inode), &er);
}

/**
 * gfs_listxattr - 
 * @dentry:
 * @buffer:
 * @size:
 *
 * Returns: The number of bytes put into data, or -errno
 */

ssize_t
gfs_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
	struct gfs_ea_request er;

	atomic_inc(&get_v2sdp(dentry->d_inode->i_sb)->sd_ops_inode);

	memset(&er, 0, sizeof(struct gfs_ea_request));
	er.er_data = (size) ? buffer : NULL;
	er.er_data_len = size;

	return gfs_ea_list(get_v2ip(dentry->d_inode), &er);
}

/**
 * gfs_removexattr -
 * @dentry:
 * @name:
 *
 * Returns: errno
 */

int
gfs_removexattr(struct dentry *dentry, const char *name)
{
	struct gfs_ea_request er;

	atomic_inc(&get_v2sdp(dentry->d_inode->i_sb)->sd_ops_inode);

	memset(&er, 0, sizeof(struct gfs_ea_request));
	er.er_type = gfs_ea_name2type(name, &er.er_name);
	if (er.er_type == GFS_EATYPE_UNUSED)
	        return -EOPNOTSUPP;
	er.er_name_len = strlen(er.er_name);

	return gfs_ea_remove(get_v2ip(dentry->d_inode), &er);
}

struct inode_operations gfs_file_iops = {
	.permission = gfs_permission,
	.setattr = gfs_setattr,
	.getattr = gfs_getattr,
	.setxattr = gfs_setxattr,
	.getxattr = gfs_getxattr,
	.listxattr = gfs_listxattr,
	.removexattr = gfs_removexattr,
};

struct inode_operations gfs_dev_iops = {
	.permission = gfs_permission,
	.setattr = gfs_setattr,
	.getattr = gfs_getattr,
	.setxattr = gfs_setxattr,
	.getxattr = gfs_getxattr,
	.listxattr = gfs_listxattr,
	.removexattr = gfs_removexattr,
};

struct inode_operations gfs_dir_iops = {
	.create = gfs_create,
	.lookup = gfs_lookup,
	.link = gfs_link,
	.unlink = gfs_unlink,
	.symlink = gfs_symlink,
	.mkdir = gfs_mkdir,
	.rmdir = gfs_rmdir,
	.mknod = gfs_mknod,
	.rename = gfs_rename,
	.permission = gfs_permission,
	.setattr = gfs_setattr,
	.getattr = gfs_getattr,
	.setxattr = gfs_setxattr,
	.getxattr = gfs_getxattr,
	.listxattr = gfs_listxattr,
	.removexattr = gfs_removexattr,
};

struct inode_operations gfs_symlink_iops = {
	.readlink = gfs_readlink,
	.follow_link = gfs_follow_link,
	.permission = gfs_permission,
	.setattr = gfs_setattr,
	.getattr = gfs_getattr,
	.setxattr = gfs_setxattr,
	.getxattr = gfs_getxattr,
	.listxattr = gfs_listxattr,
	.removexattr = gfs_removexattr,
};

