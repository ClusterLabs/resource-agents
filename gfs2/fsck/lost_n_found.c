#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include "fsck.h"
#include "libgfs2.h"
#include "lost_n_found.h"
#include "link.h"

/* add_inode_to_lf - Add dir entry to lost+found for the inode
 * @ip: inode to add to lost + found
 *
 * This function adds an entry into the lost and found dir
 * for the given inode.  The name of the entry will be
 * "lost_<ip->i_num.no_addr>".
 *
 * Returns: 0 on success, -1 on failure.
 */
int add_inode_to_lf(struct gfs2_inode *ip){
	char tmp_name[256];
	char *filename;
	int filename_len;
	__be32 inode_type;

	if(!lf_dip) {
		struct gfs2_block_query q = {0};

		log_info("Locating/Creating lost and found directory\n");

        lf_dip = createi(ip->i_sbd->md.rooti, "lost+found", S_IFDIR | 0700, 0);
	if(gfs2_block_check(ip->i_sbd, bl, lf_dip->i_di.di_num.no_addr, &q)) {
			stack;
			return -1;
		}
		if(q.block_type != gfs2_inode_dir) {
			/* This is a new lost+found directory, so set its
			 * block type and increment link counts for
			 * the directories */
			/* FIXME: i'd feel better about this if
			 * fs_mkdir returned whether it created a new
			 * directory or just found an old one, and we
			 * used that instead of the block_type to run
			 * this */
			gfs2_block_set(ip->i_sbd, bl,
				       lf_dip->i_di.di_num.no_addr, gfs2_inode_dir);
			increment_link(ip->i_sbd,
						   ip->i_sbd->md.rooti->i_di.di_num.no_addr);
			increment_link(ip->i_sbd, lf_dip->i_di.di_num.no_addr);
			increment_link(ip->i_sbd, lf_dip->i_di.di_num.no_addr);
		}
	}
	if(ip->i_di.di_num.no_addr == lf_dip->i_di.di_num.no_addr) {
		log_err("Trying to add lost+found to itself...skipping");
		return 0;
	}
	switch(ip->i_di.di_mode & S_IFMT){
	case S_IFDIR:
		log_info("Adding .. entry pointing to lost+found for %"PRIu64"\n",
				 ip->i_di.di_num.no_addr);
		sprintf(tmp_name, "..");
		filename_len = strlen(tmp_name);  /* no trailing NULL */
		if(!(filename = malloc((sizeof(char) * filename_len) + 1))) {
			log_err("Unable to allocate name\n");
			stack;
			return -1;
		}
		if(!memset(filename, 0, (sizeof(char) * filename_len) + 1)) {
			log_err("Unable to zero name\n");
			stack;
			return -1;
		}
		memcpy(filename, tmp_name, filename_len);

		if(gfs2_dirent_del(ip, NULL, filename, filename_len))
			log_warn("add_inode_to_lf:  "
					 "Unable to remove \"..\" directory entry.\n");

		dir_add(ip, filename, filename_len, &(lf_dip->i_di.di_num), DT_DIR);
		free(filename);
		sprintf(tmp_name, "lost_dir_%llu",
			(unsigned long long)ip->i_di.di_num.no_addr);
		inode_type = DT_DIR;
		break;
	case S_IFREG:
		sprintf(tmp_name, "lost_file_%llu",
			(unsigned long long)ip->i_di.di_num.no_addr);
		inode_type = DT_REG;
		break;
	case S_IFLNK:
		sprintf(tmp_name, "lost_link_%llu",
			(unsigned long long)ip->i_di.di_num.no_addr);
		inode_type = DT_LNK;
		break;
	case S_IFBLK:
		sprintf(tmp_name, "lost_blkdev_%llu",
			(unsigned long long)ip->i_di.di_num.no_addr);
		inode_type = DT_BLK;
		break;
	case S_IFCHR:
		sprintf(tmp_name, "lost_chrdev_%llu",
			(unsigned long long)ip->i_di.di_num.no_addr);
		inode_type = DT_CHR;
		break;
	case S_IFIFO:
		sprintf(tmp_name, "lost_fifo_%llu",
			(unsigned long long)ip->i_di.di_num.no_addr);
		inode_type = DT_FIFO;
		break;
	case S_IFSOCK:
		sprintf(tmp_name, "lost_socket_%llu",
			(unsigned long long)ip->i_di.di_num.no_addr);
		inode_type = DT_SOCK;
		break;
	default:
		sprintf(tmp_name, "lost_%llu",
			(unsigned long long)ip->i_di.di_num.no_addr);
		inode_type = DT_REG;
		break;
	}
	filename_len = strlen(tmp_name);  /* no trailing NULL */
	if(!(filename = malloc(sizeof(char) * filename_len))) {
		log_err("Unable to allocate name\n");
			stack;
			return -1;
		}
	if(!memset(filename, 0, sizeof(char) * filename_len)) {
		log_err("Unable to zero name\n");
		stack;
		return -1;
	}
	memcpy(filename, tmp_name, filename_len);

	dir_add(lf_dip, filename, filename_len, &(ip->i_di.di_num), inode_type);
  	increment_link(ip->i_sbd, ip->i_di.di_num.no_addr);
	if(S_ISDIR(ip->i_di.di_mode))
		increment_link(ip->i_sbd, lf_dip->i_di.di_num.no_addr);

	free(filename);
	log_notice("Added inode #%"PRIu64" to lost+found dir\n",
			   ip->i_di.di_num.no_addr);
	return 0;
}
