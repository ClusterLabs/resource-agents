#ifndef _METAWALK_H
#define _METAWALK_H

#define DIR_LINEAR 1
#define DIR_EXHASH 2

struct metawalk_fxns;

int check_inode_eattr(struct gfs2_inode *ip, struct metawalk_fxns *pass);
int check_metatree(struct gfs2_inode *ip, struct metawalk_fxns *pass);
int check_dir(struct gfs2_sbd *sbp, uint64_t block,
			  struct metawalk_fxns *pass);
int remove_dentry_from_dir(struct gfs2_sbd *sbp, uint64_t dir,
						   uint64_t dentryblock);
int find_di(struct gfs2_sbd *sbp, uint64_t childblock, struct dir_info **dip);
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
	int (*check_leaf) (struct gfs2_inode *ip, uint64_t block,
			   struct gfs2_buffer_head *bh, void *private);
	int (*check_metalist) (struct gfs2_inode *ip, uint64_t block,
			       struct gfs2_buffer_head **bh, void *private);
	int (*check_data) (struct gfs2_inode *ip, uint64_t block,
			   void *private);
	int (*check_eattr_indir) (struct gfs2_inode *ip, uint64_t block,
				  uint64_t parent,
				  struct gfs2_buffer_head **bh, void *private);
	int (*check_eattr_leaf) (struct gfs2_inode *ip, uint64_t block,
				 uint64_t parent, struct gfs2_buffer_head **bh,
				 void *private);
	int (*check_dentry) (struct gfs2_inode *ip, struct gfs2_dirent *de,
			     struct gfs2_dirent *prev,
			     struct gfs2_buffer_head *bh,
			     char *filename, int *update, uint16_t *count,
			     void *private);
	int (*check_eattr_entry) (struct gfs2_inode *ip,
				  struct gfs2_buffer_head *leaf_bh,
				  struct gfs2_ea_header *ea_hdr,
				  struct gfs2_ea_header *ea_hdr_prev,
				  void *private);
	int (*check_eattr_extentry) (struct gfs2_inode *ip,
				     uint64_t *ea_data_ptr,
				     struct gfs2_buffer_head *leaf_bh,
				     struct gfs2_ea_header *ea_hdr,
				     struct gfs2_ea_header *ea_hdr_prev,
				     void *private);
};

#endif /* _METAWALK_H */
