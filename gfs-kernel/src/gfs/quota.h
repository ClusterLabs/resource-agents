#ifndef __QUOTA_DOT_H__
#define __QUOTA_DOT_H__

#define NO_QUOTA_CHANGE ((uint32_t)-1)

int gfs_quota_get(struct gfs_sbd *sdp, int user, uint32_t id, int create,
		     struct gfs_quota_data **qdp);
void gfs_quota_hold(struct gfs_sbd *sdp, struct gfs_quota_data *qd);
void gfs_quota_put(struct gfs_sbd *sdp, struct gfs_quota_data *qd);

int gfs_quota_merge(struct gfs_sbd *sdp, struct gfs_quota_tag *tag);
void gfs_quota_scan(struct gfs_sbd *sdp);
void gfs_quota_cleanup(struct gfs_sbd *sdp);

int gfs_quota_hold_m(struct gfs_inode *ip, uint32_t uid, uint32_t gid);
void gfs_quota_unhold_m(struct gfs_inode *ip);

int gfs_quota_lock_m(struct gfs_inode *ip, uint32_t uid, uint32_t gid);
void gfs_quota_unlock_m(struct gfs_inode *ip);

int gfs_quota_check(struct gfs_inode *ip, uint32_t uid, uint32_t gid);

int gfs_quota_sync(struct gfs_sbd *sdp);
int gfs_quota_refresh(struct gfs_sbd *sdp, int user, uint32_t id);
int gfs_quota_read(struct gfs_sbd *sdp, int user, uint32_t id,
		   struct gfs_quota *q);

#endif /* __QUOTA_DOT_H__ */
