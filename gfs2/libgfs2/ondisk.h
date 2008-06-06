#ifndef __ONDISK_DOT_H__
#define __ONDISK_DOT_H__


/* Translation functions */

extern void gfs2_inum_in(struct gfs2_inum *no, char *buf);
extern void gfs2_inum_out(struct gfs2_inum *no, char *buf);
extern void gfs2_meta_header_in(struct gfs2_meta_header *mh, char *buf);
extern void gfs2_meta_header_out(struct gfs2_meta_header *mh, char *buf);
extern void gfs2_sb_in(struct gfs2_sb *sb, char *buf);
extern void gfs2_sb_out(struct gfs2_sb *sb, char *buf);
extern void gfs2_rindex_in(struct gfs2_rindex *ri, char *buf);
extern void gfs2_rindex_out(struct gfs2_rindex *ri, char *buf);
extern void gfs2_rgrp_in(struct gfs2_rgrp *rg, char *buf);
extern void gfs2_rgrp_out(struct gfs2_rgrp *rg, char *buf);
extern void gfs2_quota_in(struct gfs2_quota *qu, char *buf);
extern void gfs2_quota_out(struct gfs2_quota *qu, char *buf);
extern void gfs2_dinode_in(struct gfs2_dinode *di, char *buf);
extern void gfs2_dinode_out(struct gfs2_dinode *di, char *buf);
extern void gfs2_dirent_in(struct gfs2_dirent *de, char *buf);
extern void gfs2_dirent_out(struct gfs2_dirent *de, char *buf);
extern void gfs2_leaf_in(struct gfs2_leaf *lf, char *buf);
extern void gfs2_leaf_out(struct gfs2_leaf *lf, char *buf);
extern void gfs2_ea_header_in(struct gfs2_ea_header *ea, char *buf);
extern void gfs2_ea_header_out(struct gfs2_ea_header *ea, char *buf);
extern void gfs2_log_header_in(struct gfs2_log_header *lh, char *buf);
extern void gfs2_log_header_out(struct gfs2_log_header *lh, char *buf);
extern void gfs2_log_descriptor_in(struct gfs2_log_descriptor *ld, char *buf);
extern void gfs2_log_descriptor_out(struct gfs2_log_descriptor *ld, char *buf);
extern void gfs2_inum_range_in(struct gfs2_inum_range *ir, char *buf);
extern void gfs2_inum_range_out(struct gfs2_inum_range *ir, char *buf);
extern void gfs2_statfs_change_in(struct gfs2_statfs_change *sc, char *buf);
extern void gfs2_statfs_change_out(struct gfs2_statfs_change *sc, char *buf);
extern void gfs2_quota_change_in(struct gfs2_quota_change *qc, char *buf);
extern void gfs2_quota_change_out(struct gfs2_quota_change *qc, char *buf);

/* Printing functions */

extern void gfs2_inum_print(struct gfs2_inum *no);
extern void gfs2_meta_header_print(struct gfs2_meta_header *mh);
extern void gfs2_sb_print(struct gfs2_sb *sb);
extern void gfs2_rindex_print(struct gfs2_rindex *ri);
extern void gfs2_rgrp_print(struct gfs2_rgrp *rg);
extern void gfs2_quota_print(struct gfs2_quota *qu);
extern void gfs2_dinode_print(struct gfs2_dinode *di);
extern void gfs2_dirent_print(struct gfs2_dirent *de, char *name);
extern void gfs2_leaf_print(struct gfs2_leaf *lf);
extern void gfs2_ea_header_print(struct gfs2_ea_header *ea, char *name);
extern void gfs2_log_header_print(struct gfs2_log_header *lh);
extern void gfs2_log_descriptor_print(struct gfs2_log_descriptor *ld);
extern void gfs2_inum_range_print(struct gfs2_inum_range *ir);
extern void gfs2_statfs_change_print(struct gfs2_statfs_change *sc);
#if 0
extern void gfs2_unlinked_tag_print(struct gfs2_unlinked_tag *ut);
#endif
extern void gfs2_quota_change_print(struct gfs2_quota_change *qc);

#endif /* __ONDISK_DOT_H__ */
