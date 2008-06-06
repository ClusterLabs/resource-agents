#ifndef _FSCK_H
#define _FSCK_H


#include "fsck_incore.h"
#include "log.h"

struct gfs_sb;
struct fsck_sb;

struct options {
	char *device;
	int yes:1;
	int no:1;
};

extern uint64_t last_fs_block, last_reported_block;
extern int skip_this_pass, fsck_abort, fsck_query;

int initialize(struct fsck_sb *sbp);
void destroy(struct fsck_sb *sbp);
int block_mounters(struct fsck_sb *sbp, int block_em);
int pass1(struct fsck_sb *sbp);
int pass1b(struct fsck_sb *sbp);
int pass1c(struct fsck_sb *sbp);
int pass2(struct fsck_sb *sbp, struct options *opts);
int pass3(struct fsck_sb *sbp, struct options *opts);
int pass4(struct fsck_sb *sbp, struct options *opts);
int pass5(struct fsck_sb *sbp, struct options *opts);

/* FIXME: Hack to get this going for pass2 - this should be pulled out
 * of pass1 and put somewhere else... */
int add_to_dir_list(struct fsck_sb *sbp, uint64_t block);

#endif /* _FSCK_H */
