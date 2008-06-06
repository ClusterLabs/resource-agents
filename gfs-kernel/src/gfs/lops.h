#ifndef __LOPS_DOT_H__
#define __LOPS_DOT_H__

extern struct gfs_log_operations gfs_glock_lops;
extern struct gfs_log_operations gfs_buf_lops;
extern struct gfs_log_operations gfs_unlinked_lops;
extern struct gfs_log_operations gfs_quota_lops;

extern struct gfs_log_operations *gfs_log_ops[];

#define INIT_LE(le, lops) \
do \
{ \
  (le)->le_ops = (lops); \
  (le)->le_trans = NULL; \
  INIT_LIST_HEAD(&(le)->le_list); \
} \
while (0)

#define LO_ADD(sdp, le) \
do \
{ \
  if ((le)->le_ops->lo_add) \
    (le)->le_ops->lo_add((sdp), (le)); \
} \
while (0)

#define LO_TRANS_END(sdp, le) \
do \
{ \
  if ((le)->le_ops->lo_trans_end) \
    (le)->le_ops->lo_trans_end((sdp), (le)); \
} \
while (0)

#define LO_PRINT(sdp, le, where) \
do \
{ \
  if ((le)->le_ops->lo_print) \
    (le)->le_ops->lo_print((sdp), (le), (where)); \
} \
while (0)

static __inline__ struct gfs_trans *
LO_OVERLAP_TRANS(struct gfs_sbd *sdp, struct gfs_log_element *le)
{
	if (le->le_ops->lo_overlap_trans)
		return le->le_ops->lo_overlap_trans(sdp, le);
	else
		return NULL;
}

#define LO_INCORE_COMMIT(sdp, tr, le) \
do \
{ \
  if ((le)->le_ops->lo_incore_commit) \
    (le)->le_ops->lo_incore_commit((sdp), (tr), (le)); \
} \
while (0)

#define LO_ADD_TO_AIL(sdp, le) \
do \
{ \
  if ((le)->le_ops->lo_add_to_ail) \
    (le)->le_ops->lo_add_to_ail((sdp), (le)); \
} \
while (0)

#define LO_CLEAN_DUMP(sdp, le) \
do \
{ \
  if ((le)->le_ops->lo_clean_dump) \
    (le)->le_ops->lo_clean_dump((sdp), (le)); \
} \
while (0)

#define LO_TRANS_SIZE(sdp, tr, mblks, eblks, blocks, bmem) \
do \
{ \
  int __lops_x; \
  for (__lops_x = 0; gfs_log_ops[__lops_x]; __lops_x++) \
    if (gfs_log_ops[__lops_x]->lo_trans_size) \
      gfs_log_ops[__lops_x]->lo_trans_size((sdp), (tr), (mblks), (eblks), (blocks), (bmem)); \
} \
while (0)

#define LO_TRANS_COMBINE(sdp, tr, new_tr) \
do \
{ \
  int __lops_x; \
  for (__lops_x = 0; gfs_log_ops[__lops_x]; __lops_x++) \
    if (gfs_log_ops[__lops_x]->lo_trans_combine) \
      gfs_log_ops[__lops_x]->lo_trans_combine((sdp), (tr), (new_tr)); \
} \
while (0)

#define LO_BUILD_BHLIST(sdp, tr) \
do \
{ \
  int __lops_x; \
  for (__lops_x = 0; gfs_log_ops[__lops_x]; __lops_x++) \
    if (gfs_log_ops[__lops_x]->lo_build_bhlist) \
      gfs_log_ops[__lops_x]->lo_build_bhlist((sdp), (tr)); \
} \
while (0)

#define LO_DUMP_SIZE(sdp, elements, blocks, bmem) \
do \
{ \
  int __lops_x; \
  for (__lops_x = 0; gfs_log_ops[__lops_x]; __lops_x++) \
    if (gfs_log_ops[__lops_x]->lo_dump_size) \
      gfs_log_ops[__lops_x]->lo_dump_size((sdp), (elements), (blocks), (bmem)); \
} \
while (0)

#define LO_BUILD_DUMP(sdp, tr) \
do \
{ \
  int __lops_x; \
  for (__lops_x = 0; gfs_log_ops[__lops_x]; __lops_x++) \
    if (gfs_log_ops[__lops_x]->lo_build_dump) \
      gfs_log_ops[__lops_x]->lo_build_dump((sdp), (tr)); \
} \
while (0)

#define LO_BEFORE_SCAN(sdp, jid, head, pass) \
do \
{ \
  int __lops_x; \
  for (__lops_x = 0; gfs_log_ops[__lops_x]; __lops_x++) \
    if (gfs_log_ops[__lops_x]->lo_before_scan) \
      gfs_log_ops[__lops_x]->lo_before_scan((sdp), (jid), (head), (pass)); \
} \
while (0)

static __inline__ int
LO_SCAN_ELEMENTS(struct gfs_sbd *sdp, struct gfs_jindex *jdesc,
		 struct gfs_glock *gl, uint64_t start,
		 struct gfs_log_descriptor *desc, unsigned int pass)
{
	int x;
	int error;

	for (x = 0; gfs_log_ops[x]; x++)
		if (gfs_log_ops[x]->lo_scan_elements) {
			error = gfs_log_ops[x]->lo_scan_elements(sdp, jdesc, gl,
								 start, desc, pass);
			if (error)
				return error;
		}

	return 0;
}

#define LO_AFTER_SCAN(sdp, jid, pass) \
do \
{ \
  int __lops_x; \
  for (__lops_x = 0; gfs_log_ops[__lops_x]; __lops_x++) \
    if (gfs_log_ops[__lops_x]->lo_after_scan) \
      gfs_log_ops[__lops_x]->lo_after_scan((sdp), (jid), (pass)); \
} \
while (0)

#endif /* __LOPS_DOT_H__ */
