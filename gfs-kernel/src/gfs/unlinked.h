#ifndef __UNLINKED_DOT_H__
#define __UNLINKED_DOT_H__

struct gfs_unlinked *gfs_unlinked_get(struct gfs_sbd *sdp,
				      struct gfs_inum *inum, int create);
void gfs_unlinked_hold(struct gfs_sbd *sdp, struct gfs_unlinked *ul);
void gfs_unlinked_put(struct gfs_sbd *sdp, struct gfs_unlinked *ul);

void gfs_unlinked_lock(struct gfs_sbd *sdp, struct gfs_unlinked *ul);
void gfs_unlinked_unlock(struct gfs_sbd *sdp, struct gfs_unlinked *ul);

void gfs_unlinked_merge(struct gfs_sbd *sdp, unsigned int type,
			struct gfs_inum *inum);
void gfs_unlinked_cleanup(struct gfs_sbd *sdp);

void gfs_unlinked_limit(struct gfs_sbd *sdp);
void gfs_unlinked_dealloc(struct gfs_sbd *sdp);

#endif /* __UNLINKED_DOT_H__ */
