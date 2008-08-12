#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/exportfs.h>

#include "gfs.h"
#include "dio.h"
#include "dir.h"
#include "glock.h"
#include "glops.h"
#include "inode.h"
#include "ops_dentry.h"
#include "ops_export.h"
#include "rgrp.h"

struct inode_cookie
{
	uint64_t formal_ino;
	uint32_t gen;
	int gen_valid;
};

struct get_name_filldir
{
	uint64_t formal_ino;
	char *name;
};

/**
 * gfs_encode_fh -
 * @param1: description
 * @param2: description
 * @param3: description
 *
 * Function description
 *
 * Returns: what is returned
 */

int 
gfs_encode_fh(struct dentry *dentry, __u32 *fh, int *len,
	      int connectable)
{
	struct inode *inode = dentry->d_inode;
	struct gfs_inode *ip = get_v2ip(inode);
	int maxlen = *len;

	atomic_inc(&ip->i_sbd->sd_ops_export);

	if (maxlen < 3)
		return 255;

	fh[0] = cpu_to_gfs32((uint32_t)(ip->i_num.no_formal_ino >> 32));
	fh[1] = cpu_to_gfs32((uint32_t)(ip->i_num.no_formal_ino & 0xFFFFFFFF));
	fh[2] = cpu_to_gfs32(inode->i_generation);  /* dinode's mh_incarn */
	*len = 3;

	if (maxlen < 5 || !connectable)
		return 3;

	spin_lock(&dentry->d_lock);

	inode = dentry->d_parent->d_inode;
	ip = get_v2ip(inode);

	fh[3] = cpu_to_gfs32((uint32_t)(ip->i_num.no_formal_ino >> 32));
	fh[4] = cpu_to_gfs32((uint32_t)(ip->i_num.no_formal_ino & 0xFFFFFFFF));
	*len = 5;

	if (maxlen < 6) {
		spin_unlock(&dentry->d_lock);
		return 5;
	}

	fh[5] = cpu_to_gfs32(inode->i_generation);  /* dinode's mh_incarn */

	spin_unlock(&dentry->d_lock);

	*len = 6;

	return 6;
}

/**
 * get_name_filldir - 
 * @param1: description
 * @param2: description
 * @param3: description
 *
 * Function description
 *
 * Returns: what is returned
 */

static int
get_name_filldir(void *opaque,
		 const char *name, unsigned int length,
		 uint64_t offset,
		 struct gfs_inum *inum, unsigned int type)
{
	struct get_name_filldir *gnfd = (struct get_name_filldir *)opaque;

	if (inum->no_formal_ino != gnfd->formal_ino)
		return 0;

	memcpy(gnfd->name, name, length);
	gnfd->name[length] = 0;

	return 1;
}

/**
 * gfs_get_name -
 * @param1: description
 * @param2: description
 * @param3: description
 *
 * Function description
 *
 * Returns: what is returned
 */

int gfs_get_name(struct dentry *parent, char *name,
		 struct dentry *child)
{
	struct inode *dir = parent->d_inode;
	struct inode *inode = child->d_inode;
	struct gfs_inode *dip, *ip;
	struct get_name_filldir gnfd;
	struct gfs_holder gh;
	uint64_t offset = 0;
	int error;

	if (!dir)
		return -EINVAL;

	atomic_inc(&get_v2sdp(dir->i_sb)->sd_ops_export);

	if (!S_ISDIR(dir->i_mode) || !inode)
		return -EINVAL;

	dip = get_v2ip(dir);
	ip = get_v2ip(inode);

	*name = 0;
	gnfd.formal_ino = ip->i_num.no_formal_ino;
	gnfd.name = name;

	error = gfs_glock_nq_init(dip->i_gl, LM_ST_SHARED, 0, &gh);
	if (error)
		return error;

	error = gfs_dir_read(dip, &offset, &gnfd, get_name_filldir);

	gfs_glock_dq_uninit(&gh);

	if (!error & !*name)
		error = -ENOENT;

	return error;
}

/**
 * gfs_get_parent -
 * @param1: description
 * @param2: description
 * @param3: description
 *
 * Function description
 *
 * Returns: what is returned
 */

struct dentry *
gfs_get_parent(struct dentry *child)
{
	struct gfs_inode *dip = get_v2ip(child->d_inode);
	struct gfs_holder d_gh, i_gh;
	struct qstr dotdot = { .name = "..", .len = 2 };
	struct gfs_inode *ip;
	struct inode *inode;
	struct dentry *dentry;
	int error;

	atomic_inc(&dip->i_sbd->sd_ops_export);

	gfs_holder_init(dip->i_gl, 0, 0, &d_gh);
	error = gfs_lookupi(&d_gh, &dotdot, TRUE, &i_gh);
	if (error)
		goto fail;

	error = -ENOENT;
	if (!i_gh.gh_gl)
		goto fail;

	ip = get_gl2ip(i_gh.gh_gl);

	inode = gfs_iget(ip, CREATE);
	gfs_inode_put(ip);

	gfs_glock_dq_uninit(&d_gh);
	gfs_glock_dq_uninit(&i_gh);

	if (!inode)
		return ERR_PTR(-ENOMEM);

	dentry = d_alloc_anon(inode);
	if (!dentry) {
		iput(inode);
		return ERR_PTR(-ENOMEM);
	}

	dentry->d_op = &gfs_dops;
	return dentry;

 fail:
	gfs_holder_uninit(&d_gh);
	return ERR_PTR(error);
}

/**
 * gfs_get_dentry -
 * @param1: description
 * @param2: description
 * @param3: description
 *
 * Function description
 *
 * Returns: what is returned
 */

struct dentry *
gfs_get_dentry(struct super_block *sb, struct inode_cookie *cookie)
{
	struct gfs_sbd *sdp = get_v2sdp(sb);
	struct gfs_inum inum;
	struct gfs_holder i_gh, ri_gh, rgd_gh;
	struct gfs_rgrpd *rgd;
	struct buffer_head *bh;
	struct gfs_dinode *di;
	struct gfs_inode *ip;
	struct inode *inode;
	struct dentry *dentry;
	int error;

	atomic_inc(&sdp->sd_ops_export);

	if (!cookie->formal_ino ||
	    cookie->formal_ino == sdp->sd_jiinode->i_num.no_formal_ino ||
	    cookie->formal_ino == sdp->sd_riinode->i_num.no_formal_ino ||
	    cookie->formal_ino == sdp->sd_qinode->i_num.no_formal_ino)
		return ERR_PTR(-EINVAL);

	inum.no_formal_ino = cookie->formal_ino;
	inum.no_addr = cookie->formal_ino;

	error = gfs_glock_nq_num(sdp,
				 inum.no_formal_ino, &gfs_inode_glops,
				 LM_ST_SHARED, LM_FLAG_ANY | GL_LOCAL_EXCL,
				 &i_gh);
	if (error)
		return ERR_PTR(error);

	error = gfs_inode_get(i_gh.gh_gl, &inum, NO_CREATE, &ip);
	if (error)
		goto fail;
	if (ip)
		goto out;

	error = gfs_rindex_hold(sdp, &ri_gh);
	if (error)
		goto fail;

	error = -EINVAL;
	rgd = gfs_blk2rgrpd(sdp, inum.no_addr);
	if (!rgd)
		goto fail_rindex;

	error = gfs_glock_nq_init(rgd->rd_gl, LM_ST_SHARED, 0, &rgd_gh);
	if (error)
		goto fail_rindex;

	error = -ESTALE;
	if (gfs_get_block_type(rgd, inum.no_addr) != GFS_BLKST_USEDMETA)
		goto fail_rgd;

	error = gfs_dread(i_gh.gh_gl, inum.no_addr,
			  DIO_START | DIO_WAIT, &bh);
	if (error)
		goto fail_rgd;

	di = (struct gfs_dinode *)bh->b_data;

	error = -ESTALE;
	if (gfs32_to_cpu(di->di_header.mh_magic) != GFS_MAGIC ||
	    gfs32_to_cpu(di->di_header.mh_type) != GFS_METATYPE_DI ||
	    (gfs32_to_cpu(di->di_flags) & GFS_DIF_UNUSED))
		goto fail_relse;

	brelse(bh);
	gfs_glock_dq_uninit(&rgd_gh);
	gfs_glock_dq_uninit(&ri_gh);

	error = gfs_inode_get(i_gh.gh_gl, &inum, CREATE, &ip);
	if (error)
		goto fail;

	atomic_inc(&sdp->sd_fh2dentry_misses);

 out:
	inode = gfs_iget(ip, CREATE);
	gfs_inode_put(ip);

	gfs_glock_dq_uninit(&i_gh);

	if (!inode)
		return ERR_PTR(-ENOMEM);

	/* inode->i_generation is GFS dinode's mh_incarn value */
	if (cookie->gen_valid && cookie->gen != inode->i_generation) {
		iput(inode);
		return ERR_PTR(-ESTALE);
	}

	dentry = d_alloc_anon(inode);
	if (!dentry) {
		iput(inode);
		return ERR_PTR(-ENOMEM);
	}

	dentry->d_op = &gfs_dops;
	return dentry;

 fail_relse:
        brelse(bh);

 fail_rgd:
	gfs_glock_dq_uninit(&rgd_gh);

 fail_rindex:
	gfs_glock_dq_uninit(&ri_gh);

 fail:
	gfs_glock_dq_uninit(&i_gh);
	return ERR_PTR(error);
}

static struct dentry *gfs_fh_to_dentry(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	struct inode_cookie this;
	__u32 *fh = fid->raw;

	atomic_inc(&get_v2sdp(sb)->sd_ops_export);

	switch (fh_type) {
	case 6:
	case 5:
	case 3:
		this.gen_valid = TRUE;
		this.gen = gfs32_to_cpu(fh[2]);
		this.formal_ino = ((uint64_t)gfs32_to_cpu(fh[0])) << 32;
		this.formal_ino |= (uint64_t)gfs32_to_cpu(fh[1]);
		return gfs_get_dentry(sb, &this);
	default:
		return NULL;
	}
}

static struct dentry *gfs_fh_to_parent(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	struct inode_cookie parent;
	__u32 *fh = fid->raw;

	atomic_inc(&get_v2sdp(sb)->sd_ops_export);

	switch (fh_type) {
	case 6:
		parent.gen_valid = TRUE;
		parent.gen = gfs32_to_cpu(fh[5]);
	case 5:
		parent.formal_ino = ((uint64_t)gfs32_to_cpu(fh[3])) << 32;
		parent.formal_ino |= (uint64_t)gfs32_to_cpu(fh[4]);
	default:
		return NULL;
	}

	return gfs_get_dentry(sb, &parent);
}

const struct export_operations gfs_export_ops = {
	.encode_fh = gfs_encode_fh,
	.fh_to_dentry = gfs_fh_to_dentry,
	.fh_to_parent = gfs_fh_to_parent,
	.get_name = gfs_get_name,
	.get_parent = gfs_get_parent,
};

