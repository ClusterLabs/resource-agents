/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __LM_DOT_H__
#define __LM_DOT_H__

int gfs_lm_mount(struct gfs_sbd *sdp, int silent);
void gfs_lm_others_may_mount(struct gfs_sbd *sdp);
void gfs_lm_unmount(struct gfs_sbd *sdp);
int gfs_lm_withdraw(struct gfs_sbd *sdp, char *fmt, ...)
__attribute__ ((format(printf, 2, 3)));
int gfs_lm_get_lock(struct gfs_sbd *sdp,
		    struct lm_lockname *name, void **lockp);
void gfs_lm_put_lock(struct gfs_sbd *sdp, void *lock);
unsigned int gfs_lm_lock(struct gfs_sbd *sdp, void *lock,
			 unsigned int cur_state, unsigned int req_state,
			 unsigned int flags);
unsigned int gfs_lm_unlock(struct gfs_sbd *sdp, void *lock,
			   unsigned int cur_state);
void gfs_lm_cancel(struct gfs_sbd *sdp, void *lock);
int gfs_lm_hold_lvb(struct gfs_sbd *sdp, void *lock, char **lvbp);
void gfs_lm_unhold_lvb(struct gfs_sbd *sdp, void *lock, char *lvb);
int gfs_lm_plock_get(struct gfs_sbd *sdp,
		     struct lm_lockname *name,
		     struct file *file, struct file_lock *fl);
int gfs_lm_plock(struct gfs_sbd *sdp,
		 struct lm_lockname *name,
		 struct file *file, int cmd, struct file_lock *fl);
int gfs_lm_punlock(struct gfs_sbd *sdp,
		   struct lm_lockname *name,
		   struct file *file, struct file_lock *fl);
void gfs_lm_recovery_done(struct gfs_sbd *sdp,
			  unsigned int jid, unsigned int message);

#endif /* __LM_DOT_H__ */
