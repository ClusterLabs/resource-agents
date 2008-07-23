#ifndef _FSCK_H
#define _FSCK_H

#include "libgfs2.h"

#define FSCK_HASH_SHIFT         (13)
#define FSCK_HASH_SIZE          (1 << FSCK_HASH_SHIFT)
#define FSCK_HASH_MASK          (FSCK_HASH_SIZE - 1)

#define query(opts, fmt, args...) gfs2_query(&fsck_abort, opts, fmt, ##args)

struct inode_info
{
        osi_list_t list;
        uint64_t   inode;
        uint16_t   link_count;   /* the number of links the inode
                                  * thinks it has */
        uint16_t   counted_links; /* the number of links we've found */
};

struct dir_info
{
        osi_list_t list;
        uint64_t dinode;
        uint64_t treewalk_parent;
        uint64_t dotdot_parent;
        uint8_t  checked:1;

};

struct dir_status {
	uint8_t dotdir:1;
	uint8_t dotdotdir:1;
	struct gfs2_block_query q;
	uint32_t entry_count;
};

enum rgindex_trust_level { /* how far can we trust our RG index? */
	blind_faith = 0, /* We'd like to trust the rgindex. We always used to
			    before bz 179069. This should cover most cases. */
	open_minded = 1, /* At least 1 RG is corrupt. Try to calculate what it
			    should be, in a perfect world where our RGs are all
			    on even boundaries. Blue sky. Chirping birds. */
	distrust = 2   /* The world isn't perfect, our RGs are not on nice neat
			  boundaries.  The fs must have been messed with by
			  gfs2_grow or something.  Count the RGs by hand. */
};

struct gfs2_inode *get_system_inode(struct gfs2_sbd *sbp, uint64_t block);
struct gfs2_inode *fsck_load_inode(struct gfs2_sbd *sbp, uint64_t block);
struct gfs2_inode *fsck_inode_get(struct gfs2_sbd *sdp,
				  struct gfs2_buffer_head *bh);
void fsck_inode_put(struct gfs2_inode *ip, enum update_flags update);

int initialize(struct gfs2_sbd *sbp);
void destroy(struct gfs2_sbd *sbp);
int block_mounters(struct gfs2_sbd *sbp, int block_em);
int pass1(struct gfs2_sbd *sbp);
int pass1b(struct gfs2_sbd *sbp);
int pass1c(struct gfs2_sbd *sbp);
int pass2(struct gfs2_sbd *sbp);
int pass3(struct gfs2_sbd *sbp);
int pass4(struct gfs2_sbd *sbp);
int pass5(struct gfs2_sbd *sbp);
int rg_repair(struct gfs2_sbd *sdp, int trust_lvl, int *rg_count);

/* FIXME: Hack to get this going for pass2 - this should be pulled out
 * of pass1 and put somewhere else... */
int add_to_dir_list(struct gfs2_sbd *sbp, uint64_t block);

extern struct gfs2_options opts;
extern struct gfs2_inode *lf_dip; /* Lost and found directory inode */
extern osi_list_t dir_hash[FSCK_HASH_SIZE];
extern osi_list_t inode_hash[FSCK_HASH_SIZE];
extern struct gfs2_block_list *bl;
extern uint64_t last_fs_block, last_reported_block;
extern int skip_this_pass, fsck_abort, fsck_query;
extern uint64_t last_data_block;
extern uint64_t first_data_block;

#endif /* _FSCK_H */
