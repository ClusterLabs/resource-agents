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

#ifndef __RECOVERY_DOT_H__
#define __RECOVERY_DOT_H__

#define GFS_RECPASS_A1  (12)
#define GFS_RECPASS_B1  (14)

void gfs_add_dirty_j(struct gfs_sbd *sdp, unsigned int jid);
void gfs_clear_dirty_j(struct gfs_sbd *sdp);

int gfs_find_jhead(struct gfs_sbd *sdp, struct gfs_jindex *jdesc,
		   struct gfs_glock *gl, struct gfs_log_header *head);
int gfs_increment_blkno(struct gfs_sbd *sdp, struct gfs_jindex *jdesc,
			struct gfs_glock *gl, uint64_t *addr,
			int skip_headers);

int gfs_recover_journal(struct gfs_sbd *sdp,
			unsigned int jid, struct gfs_jindex *jdesc,
			int wait);
void gfs_check_journals(struct gfs_sbd *sdp);

int gfs_recover_dump(struct gfs_sbd *sdp);

#endif /* __RECOVERY_DOT_H__ */
