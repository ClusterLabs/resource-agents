#ifndef _METAWALK_H
#define _METAWALK_H

#define DIR_LINEAR 1
#define DIR_EXHASH 2

struct metawalk_fxns;

int check_inode_eattr(struct fsck_inode *ip, struct metawalk_fxns *pass);
int check_metatree(struct fsck_inode *ip, struct metawalk_fxns *pass);
int check_dir(struct fsck_sb *sbp, uint64_t block, struct metawalk_fxns *pass);
int remove_dentry_from_dir(struct fsck_sb *sbp, uint64_t dir,
			   uint64_t dentryblock);
int find_di(struct fsck_sb *sbp, uint64_t childblock, struct dir_info **dip);
int dinode_hash_insert(osi_list_t *buckets, uint64_t key, struct dir_info *di);
int dinode_hash_remove(osi_list_t *buckets, uint64_t key);

/* metawalk_fxns: function pointers to check various parts of the fs
 *
 * The functions should return -1 on fatal errors, 1 if the block
 * should be skipped, and 0 on success
 *
 * private: Data that should be passed to the fxns
 * check_leaf:
 * check_metalist:
 * check_data:
 * check_eattr_indir:
 * check_eattr_leaf:
 * check_dentry:
 * check_eattr_entry:
 * check_eattr_extentry:
 */
struct metawalk_fxns {
	void *private;
	int (*check_leaf) (struct fsck_inode *ip, uint64_t block,
			   osi_buf_t *bh, void *private);
	int (*check_metalist) (struct fsck_inode *ip, uint64_t block,
			       osi_buf_t **bh, void *private);
	int (*check_data) (struct fsck_inode *ip, uint64_t block,
			   void *private);
	int (*check_eattr_indir) (struct fsck_inode *ip, uint64_t block,
				  uint64_t parent, osi_buf_t **bh,
				  void *private);
	int (*check_eattr_leaf) (struct fsck_inode *ip, uint64_t block,
				 uint64_t parent, osi_buf_t **bh,
				 void *private);
	int (*check_dentry) (struct fsck_inode *ip, struct gfs_dirent *de,
			     struct gfs_dirent *prev,
			     osi_buf_t *bh, char *filename, int *update,
			     uint16_t *count,
			     void *private);
	int (*check_eattr_entry) (struct fsck_inode *ip,
				  osi_buf_t *leaf_bh,
				  struct gfs_ea_header *ea_hdr,
				  struct gfs_ea_header *ea_hdr_prev,
				  void *private);
	int (*check_eattr_extentry) (struct fsck_inode *ip,
				     uint64_t *ea_data_ptr,
				     osi_buf_t *leaf_bh,
				     struct gfs_ea_header *ea_hdr,
				     struct gfs_ea_header *ea_hdr_prev,
				     void *private);
};

#endif /* _METAWALK_H */
