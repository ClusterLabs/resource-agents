/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __LOPS_DOT_H__
#define __LOPS_DOT_H__

extern struct gfs2_log_operations gfs2_glock_lops;
extern struct gfs2_log_operations gfs2_buf_lops;
extern struct gfs2_log_operations gfs2_revoke_lops;
extern struct gfs2_log_operations gfs2_rg_lops;
extern struct gfs2_log_operations gfs2_databuf_lops;

extern struct gfs2_log_operations *gfs2_log_ops[];

#define INIT_LE(le, lops) \
do { \
	INIT_LIST_HEAD(&(le)->le_list); \
	(le)->le_ops = (lops); \
} while (0)

#define LO_ADD(sdp, le) \
do { \
	if ((le)->le_ops->lo_add) \
		(le)->le_ops->lo_add((sdp), (le)); \
} while (0)

#define LO_INCORE_COMMIT(sdp, tr) \
do { \
	int __lops_x; \
	for (__lops_x = 0; gfs2_log_ops[__lops_x]; __lops_x++) \
		if (gfs2_log_ops[__lops_x]->lo_incore_commit) \
			gfs2_log_ops[__lops_x]->lo_incore_commit((sdp), (tr)); \
} while (0)

#define LO_BEFORE_COMMIT(sdp) \
do { \
	int __lops_x; \
	for (__lops_x = 0; gfs2_log_ops[__lops_x]; __lops_x++) \
		if (gfs2_log_ops[__lops_x]->lo_before_commit) \
			gfs2_log_ops[__lops_x]->lo_before_commit((sdp)); \
} while (0)

#define LO_AFTER_COMMIT(sdp, ai) \
do { \
	int __lops_x; \
	for (__lops_x = 0; gfs2_log_ops[__lops_x]; __lops_x++) \
		if (gfs2_log_ops[__lops_x]->lo_after_commit) \
			gfs2_log_ops[__lops_x]->lo_after_commit((sdp), (ai)); \
} while (0)

#define LO_BEFORE_SCAN(jd, head, pass) \
do \
{ \
  int __lops_x; \
  for (__lops_x = 0; gfs2_log_ops[__lops_x]; __lops_x++) \
    if (gfs2_log_ops[__lops_x]->lo_before_scan) \
      gfs2_log_ops[__lops_x]->lo_before_scan((jd), (head), (pass)); \
} \
while (0)

static inline int LO_SCAN_ELEMENTS(struct gfs2_jdesc *jd, unsigned int start,
				   struct gfs2_log_descriptor *ld,
				   unsigned int pass)
{
	unsigned int x;
	int error;

	for (x = 0; gfs2_log_ops[x]; x++)
		if (gfs2_log_ops[x]->lo_scan_elements) {
			error = gfs2_log_ops[x]->lo_scan_elements(jd, start,
								 ld, pass);
			if (error)
				return error;
		}

	return 0;
}

#define LO_AFTER_SCAN(jd, error, pass) \
do \
{ \
  int __lops_x; \
  for (__lops_x = 0; gfs2_log_ops[__lops_x]; __lops_x++) \
    if (gfs2_log_ops[__lops_x]->lo_before_scan) \
      gfs2_log_ops[__lops_x]->lo_after_scan((jd), (error), (pass)); \
} \
while (0)

#endif /* __LOPS_DOT_H__ */

