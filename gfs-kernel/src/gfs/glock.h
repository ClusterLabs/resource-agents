#ifndef __GFS_GLOCK_DOT_H__
#define __GFS_GLOCK_DOT_H__

/* Flags for lock requests; used in gfs_holder gh_flag field. */
/*	These are defined in lm_interface.h, commented out here.
#define LM_FLAG_TRY       (0x00000001)
#define LM_FLAG_TRY_1CB   (0x00000002)
#define LM_FLAG_NOEXP     (0x00000004)
#define LM_FLAG_ANY       (0x00000008)
#define LM_FLAG_PRIORITY  (0x00000010)
	These are defined here. */
#define GL_LOCAL_EXCL     (0x00000020) /* Only one holder may be granted the
                                        * lock on this node, even if SHARED */
#define GL_ASYNC          (0x00000040) /* Don't block waiting for lock ...
                                        * must poll to wait for grant */
#define GL_EXACT          (0x00000080) /* Requested state must == current state
                                        * for lock to be granted */
#define GL_SKIP           (0x00000100) /* Don't read from disk after grant */
#define GL_ATIME          (0x00000200) /* Update inode's ATIME after grant */
#define GL_NOCACHE        (0x00000400) /* Release glock when done, don't cache */
#define GL_SYNC           (0x00000800) /* Sync to disk when no more holders */
#define GL_NOCANCEL       (0x00001000) /* Don't ever cancel this request */
#define GL_READPAGE       (0x00002000) /* gfs_readpage() issued this lock request */
#define GL_NOCANCEL_OTHER (0x00004000) /* Don't cancel other locks for this */

#define GLR_TRYFAILED     (13)
#define GLR_CANCELED      (14)

static __inline__ struct gfs_holder*
gfs_glock_is_locked_by_me(struct gfs_glock *gl)
{
	struct list_head *tmp, *head;
	struct gfs_holder *gh;

	/* Look in glock's list of holders for one with current task as owner */
	spin_lock(&gl->gl_spin);
	for (head = &gl->gl_holders, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		gh = list_entry(tmp, struct gfs_holder, gh_list);
		if (gh->gh_owner == current)
			goto out;
	}
	gh = NULL;
out:
	spin_unlock(&gl->gl_spin);
	return gh;
}
static __inline__ int
gfs_glock_is_held_excl(struct gfs_glock *gl)
{
	return (gl->gl_state == LM_ST_EXCLUSIVE);
}
static __inline__ int
gfs_glock_is_held_dfrd(struct gfs_glock *gl)
{
	return (gl->gl_state == LM_ST_DEFERRED);
}
static __inline__ int
gfs_glock_is_held_shrd(struct gfs_glock *gl)
{
	return (gl->gl_state == LM_ST_SHARED);
}

static __inline__ int
gfs_glock_is_blocking(struct gfs_glock *gl)
{
	int ret;
	spin_lock(&gl->gl_spin);
	ret = !list_empty(&gl->gl_waiters2) || !list_empty(&gl->gl_waiters3);
	spin_unlock(&gl->gl_spin);
	return ret;
}

struct gfs_glock *gfs_glock_find(struct gfs_sbd *sdp,
				 struct lm_lockname *name);
int gfs_glock_get(struct gfs_sbd *sdp,
		  uint64_t number, struct gfs_glock_operations *glops,
		  int create, struct gfs_glock **glp);
void gfs_glock_hold(struct gfs_glock *gl);
void gfs_glock_put(struct gfs_glock *gl);

void gfs_holder_init(struct gfs_glock *gl, unsigned int state, int flags,
		     struct gfs_holder *gh);
void gfs_holder_reinit(unsigned int state, int flags, struct gfs_holder *gh);
void gfs_holder_uninit(struct gfs_holder *gh);
struct gfs_holder *gfs_holder_get(struct gfs_glock *gl, unsigned int state,
				  int flags);
void gfs_holder_put(struct gfs_holder *gh);

void gfs_glock_xmote_th(struct gfs_glock *gl, unsigned int state, int flags);
void gfs_glock_drop_th(struct gfs_glock *gl);

int gfs_glock_nq(struct gfs_holder *gh);
int gfs_glock_poll(struct gfs_holder *gh);
int gfs_glock_wait(struct gfs_holder *gh);
void gfs_glock_dq(struct gfs_holder *gh);

void gfs_glock_prefetch(struct gfs_glock *gl, unsigned int state, int flags);
void gfs_glock_force_drop(struct gfs_glock *gl);

int gfs_glock_be_greedy(struct gfs_glock *gl, unsigned int time);

int gfs_glock_nq_init(struct gfs_glock *gl, unsigned int state, int flags,
		      struct gfs_holder *gh);
void gfs_glock_dq_uninit(struct gfs_holder *gh);
int gfs_glock_nq_num(struct gfs_sbd *sdp,
		     uint64_t number, struct gfs_glock_operations *glops,
		     unsigned int state, int flags, struct gfs_holder *gh);

int gfs_glock_nq_m(unsigned int num_gh, struct gfs_holder *ghs);
void gfs_glock_dq_m(unsigned int num_gh, struct gfs_holder *ghs);

void gfs_glock_prefetch_num(struct gfs_sbd *sdp,
			    uint64_t number, struct gfs_glock_operations *glops,
			    unsigned int state, int flags);

/*  Lock Value Block functions  */

int gfs_lvb_hold(struct gfs_glock *gl);
void gfs_lvb_unhold(struct gfs_glock *gl);
void gfs_lvb_sync(struct gfs_glock *gl);

void gfs_glock_cb(void *fsdata, unsigned int type, void *data);

void gfs_try_toss_inode(struct gfs_sbd *sdp, struct gfs_inum *inum);
void gfs_iopen_go_callback(struct gfs_glock *gl, unsigned int state);

void gfs_glock_schedule_for_reclaim(struct gfs_glock *gl);
void gfs_reclaim_glock(struct gfs_sbd *sdp);

void gfs_scand_internal(struct gfs_sbd *sdp);
void gfs_gl_hash_clear(struct gfs_sbd *sdp, int wait);

int gfs_dump_lockstate(struct gfs_sbd *sdp, struct gfs_user_buffer *ub);

#endif /* __GFS_GLOCK_DOT_H__ */
