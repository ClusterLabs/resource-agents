#ifndef __INODE_DOT_H__
#define __INODE_DOT_H__

void gfs_inode_attr_in(struct gfs_inode *ip);
void gfs_inode_attr_out(struct gfs_inode *ip);
struct inode *gfs_iget(struct gfs_inode *ip, int create);

int gfs_copyin_dinode(struct gfs_inode *ip);

int gfs_inode_get(struct gfs_glock *i_gl, struct gfs_inum *inum, int create,
		    struct gfs_inode **ipp);
void gfs_inode_hold(struct gfs_inode *ip);
void gfs_inode_put(struct gfs_inode *ip);
void gfs_inode_destroy(struct gfs_inode *ip);

int gfs_inode_dealloc(struct gfs_sbd *sdp, struct gfs_inum *inum);

int gfs_change_nlink(struct gfs_inode *ip, int diff);
int gfs_lookupi(struct gfs_holder *d_gh, struct qstr *name,
		int is_root, struct gfs_holder *i_gh);
int gfs_createi(struct gfs_holder *d_gh, struct qstr *name,
		unsigned int type, unsigned int mode,
		struct gfs_holder *i_gh);
int gfs_unlinki(struct gfs_inode *dip, struct qstr *name, struct gfs_inode *ip);
int gfs_rmdiri(struct gfs_inode *dip, struct qstr *name, struct gfs_inode *ip);
int gfs_unlink_ok(struct gfs_inode *dip, struct qstr *name,
		  struct gfs_inode *ip);
int gfs_ok_to_move(struct gfs_inode *this, struct gfs_inode *to);
int gfs_readlinki(struct gfs_inode *ip, char **buf, unsigned int *len);

int gfs_glock_nq_atime(struct gfs_holder *gh);
int gfs_glock_nq_m_atime(unsigned int num_gh, struct gfs_holder *ghs);

void gfs_try_toss_vnode(struct gfs_inode *ip);

int gfs_setattr_simple(struct gfs_inode *ip, struct iattr *attr);

/*  Backwards compatibility functions  */

int gfs_alloc_qinode(struct gfs_sbd *sdp);
int gfs_alloc_linode(struct gfs_sbd *sdp);

/*  Inlines  */

static __inline__ int
gfs_is_stuffed(struct gfs_inode *ip)
{
	return !ip->i_di.di_height;
}

static __inline__ int
gfs_is_jdata(struct gfs_inode *ip)
{
	return ip->i_di.di_flags & GFS_DIF_JDATA;
}

#endif /* __INODE_DOT_H__ */
