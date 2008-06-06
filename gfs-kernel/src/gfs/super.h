#ifndef __SUPER_DOT_H__
#define __SUPER_DOT_H__

void gfs_tune_init(struct gfs_tune *gt);

int gfs_check_sb(struct gfs_sbd *sdp, struct gfs_sb *sb, int silent);
int gfs_read_sb(struct gfs_sbd *sdp, struct gfs_glock *gl, int silent);
int gfs_do_upgrade(struct gfs_sbd *sdp, struct gfs_glock *gl_sb);

static __inline__ unsigned int
gfs_num_journals(struct gfs_sbd *sdp)
{
	unsigned int num;
	down(&sdp->sd_jindex_lock);
	num = sdp->sd_journals;
	up(&sdp->sd_jindex_lock);
	return num;
}

int gfs_jindex_hold(struct gfs_sbd *sdp, struct gfs_holder *ji_gh);
void gfs_clear_journals(struct gfs_sbd *sdp);

int gfs_get_jiinode(struct gfs_sbd *sdp);
int gfs_get_riinode(struct gfs_sbd *sdp);
int gfs_get_rootinode(struct gfs_sbd *sdp);
int gfs_get_qinode(struct gfs_sbd *sdp);
int gfs_get_linode(struct gfs_sbd *sdp);

int gfs_make_fs_rw(struct gfs_sbd *sdp);
int gfs_make_fs_ro(struct gfs_sbd *sdp);

int gfs_statfs_init(struct gfs_sbd *sdp, int flag);
int gfs_statfs_sync(struct gfs_sbd *sdp);
int gfs_statfs_fast(struct gfs_sbd *sdp, void *buf);

struct gfs_stat_gfs {
	uint64_t sg_total_blocks;
	uint64_t sg_free;
	uint64_t sg_used_dinode;
	uint64_t sg_free_dinode;
	uint64_t sg_used_meta;
	uint64_t sg_free_meta;
};

int gfs_stat_gfs(struct gfs_sbd *sdp, struct gfs_stat_gfs *sg,
		 int interruptible);

int gfs_lock_fs_check_clean(struct gfs_sbd *sdp, unsigned int state,
			    struct gfs_holder *t_gh);
int gfs_freeze_fs(struct gfs_sbd *sdp);
void gfs_unfreeze_fs(struct gfs_sbd *sdp);

#endif /* __SUPER_DOT_H__ */
