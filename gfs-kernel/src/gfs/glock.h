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

#ifndef __GFS_GLOCK_DOT_H__
#define __GFS_GLOCK_DOT_H__

/*
#define LM_FLAG_TRY       (0x00000001)
#define LM_FLAG_TRY_1CB   (0x00000002)
#define LM_FLAG_NOEXP     (0x00000004)
#define LM_FLAG_ANY       (0x00000008)
#define LM_FLAG_PRIORITY  (0x00000010)
*/
#define GL_LOCAL_EXCL     (0x00000020)
#define GL_ASYNC          (0x00000040)
#define GL_EXACT          (0x00000080)
#define GL_SKIP           (0x00000100)
#define GL_ATIME          (0x00000200)
#define GL_NOCACHE        (0x00000400)
#define GL_SYNC           (0x00000800)

#define GLR_TRYFAILED     (13)
#define GLR_CANCELED      (14)

static __inline__ int
gfs_glock_is_locked_by_me(struct gfs_glock *gl)
{
	struct list_head *tmp, *head;
	struct gfs_holder *gh;
	int locked = FALSE;

	spin_lock(&gl->gl_spin);
	for (head = &gl->gl_holders, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		gh = list_entry(tmp, struct gfs_holder, gh_list);
		if (gh->gh_owner == current) {
			locked = TRUE;
			break;
		}
	}
	spin_unlock(&gl->gl_spin);

	return locked;
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

#define GFS_ASYNC_LM(sdp) ((sdp)->sd_lockstruct.ls_flags & LM_LSFLAG_ASYNC)

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

void gfs_glock_cb(lm_fsdata_t * fsdata, unsigned int type, void *data);

void gfs_try_toss_inode(struct gfs_sbd *sdp, struct gfs_inum *inum);
void gfs_iopen_go_callback(struct gfs_glock *gl, unsigned int state);

void gfs_glock_schedule_for_reclaim(struct gfs_glock *gl);
void gfs_reclaim_glock(struct gfs_sbd *sdp);

void gfs_scand_internal(struct gfs_sbd *sdp);
void gfs_gl_hash_clear(struct gfs_sbd *sdp, int wait);

int gfs_dump_lockstate(struct gfs_sbd *sdp, struct gfs_user_buffer *ub);

#endif /* __GFS_GLOCK_DOT_H__ */
