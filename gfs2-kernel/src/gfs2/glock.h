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

#ifndef __GLOCK_DOT_H__
#define __GLOCK_DOT_H__

/* Flags for lock requests; used in gfs2_holder gh_flag field. */
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
#define GL_NEVER_RECURSE  (0x00002000) /* We'll never recursively acquire */

#define GLR_TRYFAILED     (13)
#define GLR_CANCELED      (14)

static __inline__ int
gfs2_glock_is_locked_by_me(struct gfs2_glock *gl)
{
	struct list_head *tmp, *head;
	struct gfs2_holder *gh;
	int locked = FALSE;

	/* Look in glock's list of holders for one with current task as owner */
	spin_lock(&gl->gl_spin);
	for (head = &gl->gl_holders, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		gh = list_entry(tmp, struct gfs2_holder, gh_list);
		if (gh->gh_owner == current) {
			locked = TRUE;
			break;
		}
	}
	spin_unlock(&gl->gl_spin);

	return locked;
}
static __inline__ int
gfs2_glock_is_held_excl(struct gfs2_glock *gl)
{
	return (gl->gl_state == LM_ST_EXCLUSIVE);
}
static __inline__ int
gfs2_glock_is_held_dfrd(struct gfs2_glock *gl)
{
	return (gl->gl_state == LM_ST_DEFERRED);
}
static __inline__ int
gfs2_glock_is_held_shrd(struct gfs2_glock *gl)
{
	return (gl->gl_state == LM_ST_SHARED);
}

static __inline__ int
gfs2_glock_is_blocking(struct gfs2_glock *gl)
{
	int ret;
	spin_lock(&gl->gl_spin);
	ret = !list_empty(&gl->gl_waiters2) || !list_empty(&gl->gl_waiters3);
	spin_unlock(&gl->gl_spin);
	return ret;
}

struct gfs2_glock *gfs2_glock_find(struct gfs2_sbd *sdp,
				   struct lm_lockname *name);
int gfs2_glock_get(struct gfs2_sbd *sdp,
		   uint64_t number, struct gfs2_glock_operations *glops,
		   int create, struct gfs2_glock **glp);
void gfs2_glock_hold(struct gfs2_glock *gl);
void gfs2_glock_put(struct gfs2_glock *gl);

void gfs2_holder_init(struct gfs2_glock *gl, unsigned int state, int flags,
		      struct gfs2_holder *gh);
void gfs2_holder_reinit(unsigned int state, int flags, struct gfs2_holder *gh);
void gfs2_holder_uninit(struct gfs2_holder *gh);
struct gfs2_holder *gfs2_holder_get(struct gfs2_glock *gl, unsigned int state,
				    int flags);
void gfs2_holder_put(struct gfs2_holder *gh);

void gfs2_glock_xmote_th(struct gfs2_glock *gl, unsigned int state, int flags);
void gfs2_glock_drop_th(struct gfs2_glock *gl);

int gfs2_glock_nq(struct gfs2_holder *gh);
int gfs2_glock_poll(struct gfs2_holder *gh);
int gfs2_glock_wait(struct gfs2_holder *gh);
void gfs2_glock_dq(struct gfs2_holder *gh);

void gfs2_glock_prefetch(struct gfs2_glock *gl, unsigned int state, int flags);
void gfs2_glock_force_drop(struct gfs2_glock *gl);

int gfs2_glock_be_greedy(struct gfs2_glock *gl, unsigned int time);

int gfs2_glock_nq_init(struct gfs2_glock *gl, unsigned int state, int flags,
		       struct gfs2_holder *gh);
void gfs2_glock_dq_uninit(struct gfs2_holder *gh);
int gfs2_glock_nq_num(struct gfs2_sbd *sdp,
		      uint64_t number, struct gfs2_glock_operations *glops,
		      unsigned int state, int flags, struct gfs2_holder *gh);

int gfs2_glock_nq_m(unsigned int num_gh, struct gfs2_holder *ghs);
void gfs2_glock_dq_m(unsigned int num_gh, struct gfs2_holder *ghs);
void gfs2_glock_dq_uninit_m(unsigned int num_gh, struct gfs2_holder *ghs);

void gfs2_glock_prefetch_num(struct gfs2_sbd *sdp,
			     uint64_t number, struct gfs2_glock_operations *glops,
			     unsigned int state, int flags);

/*  Lock Value Block functions  */

int gfs2_lvb_hold(struct gfs2_glock *gl);
void gfs2_lvb_unhold(struct gfs2_glock *gl);
void gfs2_lvb_sync(struct gfs2_glock *gl);

void gfs2_glock_cb(lm_fsdata_t *fsdata, unsigned int type, void *data);

void gfs2_try_toss_inode(struct gfs2_sbd *sdp, struct gfs2_inum *inum);
void gfs2_iopen_go_callback(struct gfs2_glock *gl, unsigned int state);

void gfs2_glock_schedule_for_reclaim(struct gfs2_glock *gl);
void gfs2_reclaim_glock(struct gfs2_sbd *sdp);

void gfs2_scand_internal(struct gfs2_sbd *sdp);
void gfs2_gl_hash_clear(struct gfs2_sbd *sdp, int wait);

int gfs2_dump_lockstate(struct gfs2_sbd *sdp, struct gfs2_user_buffer *ub);

#endif /* __GLOCK_DOT_H__ */
