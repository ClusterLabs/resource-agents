#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <linux/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <curses.h>
#include <term.h>
#include <sys/ioctl.h>
#include <limits.h>
#include <linux_endian.h>
#include <sys/time.h>
#include <linux/gfs2_ondisk.h>

#include "osi_list.h"
#include "gfs2hex.h"
#include "hexedit.h"
#include "libgfs2.h"

#define BUFSIZE (4096)
#define DFT_SAVE_FILE "/tmp/gfsmeta"
#define MAX_JOURNALS_SAVED 256

struct saved_metablock {
	uint64_t blk;
	uint16_t siglen; /* significant data length */
	char buf[BUFSIZE];
};

struct saved_metablock *savedata;
uint64_t last_fs_block, last_reported_block, blks_saved, total_out, pct;
struct gfs2_block_list *blocklist = NULL;
uint64_t journal_blocks[MAX_JOURNALS_SAVED];
uint64_t gfs1_journal_size = 0; /* in blocks */
int journals_found = 0;

extern void read_superblock(void);
uint64_t masterblock(const char *fn);

static __inline__ int fs_is_jdata(struct gfs2_inode *ip)
{
        return ip->i_di.di_flags & GFS2_DIF_JDATA;
}

static struct metapath *find_metapath(struct gfs2_inode *ip, uint64_t block)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct metapath *mp;
	uint64_t b = block;
	unsigned int i;

	zalloc(mp, sizeof(struct metapath));

	for (i = ip->i_di.di_height; i--;)
		mp->mp_list[i] = do_div(b, sdp->sd_inptrs);

	return mp;
}

static __inline__ uint64_t *
metapointer(struct gfs2_buffer_head *bh, unsigned int height,
			struct metapath *mp)
{
	unsigned int head_size = (height > 0) ?
		sizeof(struct gfs2_meta_header) : sizeof(struct gfs2_dinode);

	return ((uint64_t *)(bh->b_data + head_size)) + mp->mp_list[height];
}

static void lookup_block(struct gfs2_inode *ip,
	     struct gfs2_buffer_head *bh, unsigned int height, struct metapath *mp,
	     int create, int *new, uint64_t *block)
{
	uint64_t *ptr = metapointer(bh, height, mp);

	if (*ptr) {
		*block = be64_to_cpu(*ptr);
		return;
	}

	*block = 0;

	if (!create)
		return;

	if (height == ip->i_di.di_height - 1&&
	    !(S_ISDIR(ip->i_di.di_mode)))
		*block = data_alloc(ip);
	else
		*block = meta_alloc(ip);

	*ptr = cpu_to_be64(*block);
	ip->i_di.di_blocks++;

	*new = 1;
}

void gfs1_block_map(struct gfs2_inode *ip, uint64_t lblock, int *new,
		    uint64_t *dblock, uint32_t *extlen, int prealloc)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_buffer_head *bh;
	struct metapath *mp;
	int create = *new;
	unsigned int bsize;
	unsigned int height;
	unsigned int end_of_metadata;
	unsigned int x;
	enum update_flags f;

	f = not_updated;
	*new = 0;
	*dblock = 0;
	if (extlen)
		*extlen = 0;

	if (!ip->i_di.di_height) { /* stuffed */
		if (!lblock) {
			*dblock = ip->i_di.di_num.no_addr;
			if (extlen)
				*extlen = 1;
		}
		return;
	}

	bsize = (fs_is_jdata(ip)) ? sdp->sd_jbsize : sdp->bsize;

	height = calc_tree_height(ip, (lblock + 1) * bsize);
	if (ip->i_di.di_height < height) {
		if (!create)
			return;

		build_height(ip, height);
	}

	mp = find_metapath(ip, lblock);
	end_of_metadata = ip->i_di.di_height - 1;

	bh = bhold(ip->i_bh);

	for (x = 0; x < end_of_metadata; x++) {
		lookup_block(ip, bh, x, mp, create, new, dblock);
		brelse(bh, not_updated);
		if (!*dblock)
			goto out;

		if (*new) {
			struct gfs2_meta_header mh;

			bh = bget(sdp, *dblock);
			mh.mh_magic = GFS2_MAGIC;
			mh.mh_type = GFS2_METATYPE_IN;
			mh.mh_format = GFS2_FORMAT_IN;
			gfs2_meta_header_out(&mh, bh->b_data);
			f = updated;
		} else
			bh = bread(sdp, *dblock);
	}

	if (!prealloc)
		lookup_block(ip, bh, end_of_metadata, mp, create, new, dblock);

	if (extlen && *dblock) {
		*extlen = 1;

		if (!*new) {
			uint64_t tmp_dblock;
			int tmp_new;
			unsigned int nptrs;

			nptrs = (end_of_metadata) ? sdp->sd_inptrs : sdp->sd_diptrs;

			while (++mp->mp_list[end_of_metadata] < nptrs) {
				lookup_block(ip, bh, end_of_metadata, mp, FALSE, &tmp_new,
							 &tmp_dblock);

				if (*dblock + *extlen != tmp_dblock)
					break;

				(*extlen)++;
			}
		}
	}

	brelse(bh, f);

 out:
	free(mp);
}

int gfs1_readi(struct gfs2_inode *ip, void *buf,
	       uint64_t offset, unsigned int size)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_buffer_head *bh;
	uint64_t lblock, dblock;
	uint32_t extlen = 0;
	unsigned int amount;
	int not_new = 0;
	int journaled = fs_is_jdata(ip);
	int copied = 0;

	if (offset >= ip->i_di.di_size)
		return 0;

	if ((offset + size) > ip->i_di.di_size)
		size = ip->i_di.di_size - offset;

	if (!size)
		return 0;

	if (journaled) {
		lblock = offset / sdp->sd_jbsize;
		offset %= sdp->sd_jbsize;
	} else {
		lblock = offset >> sdp->sd_sb.sb_bsize_shift;
		offset &= sdp->sd_sb.sb_bsize - 1;
	}

	if (!ip->i_di.di_height) /* stuffed */
		offset += sizeof(struct gfs2_dinode);
	else if (journaled)
		offset += sizeof(struct gfs2_meta_header);

	while (copied < size) {
		amount = size - copied;
		if (amount > sdp->bsize - offset)
			amount = sdp->bsize - offset;

		if (!extlen)
			gfs1_block_map(ip, lblock, &not_new, &dblock,
				       &extlen, FALSE);

		if (dblock) {
			bh = bread(sdp, dblock);
			dblock++;
			extlen--;
		} else
			bh = NULL;


		if (bh) {
			memcpy(buf+copied, bh->b_data + offset, amount);
			brelse(bh, not_updated);
		} else
			memset(buf+copied, 0, amount);
		copied += amount;
		lblock++;

		offset = (journaled) ? sizeof(struct gfs2_meta_header) : 0;
	}

	return copied;
}

/**
 * gfs1_rindex_read - read in the rg index file
 *                  Stolen from libgfs2/super.c, but modified to handle gfs1.
 * @sdp: the incore superblock pointer
 * fd: optional file handle for rindex file (if meta_fs file system is mounted)
 *     (if fd is <= zero, it will read from raw device)
 * @count1: return count of the rgs.
 *
 * Returns: 0 on success, -1 on failure
 */
int gfs1_rindex_read(struct gfs2_sbd *sdp, int fd, int *count1)
{
	unsigned int rg;
	int error;
	struct gfs2_rindex buf;
	struct rgrp_list *rgd, *prev_rgd;
	uint64_t prev_length = 0;

	*count1 = 0;
	prev_rgd = NULL;
	for (rg = 0; ; rg++) {
		if (fd > 0)
			error = read(fd, &buf, sizeof(struct gfs2_rindex));
		else
			error = gfs1_readi(sdp->md.riinode, (char *)&buf,
					   (rg * sizeof(struct gfs2_rindex)),
					   sizeof(struct gfs2_rindex));
		if (!error)
			break;
		if (error != sizeof(struct gfs2_rindex))
			return -1;

		rgd = (struct rgrp_list *)malloc(sizeof(struct rgrp_list));
		memset(rgd, 0, sizeof(struct rgrp_list));
		osi_list_add_prev(&rgd->list, &sdp->rglist);

		gfs2_rindex_in(&rgd->ri, (char *)&buf);

		rgd->start = rgd->ri.ri_addr;
		if (prev_rgd) {
			prev_length = rgd->start - prev_rgd->start;
			prev_rgd->length = prev_length;
		}

		if(gfs2_compute_bitstructs(sdp, rgd))
			return -1;

		(*count1)++;
		prev_rgd = rgd;
	}
	if (prev_rgd)
		prev_rgd->length = prev_length;
	return 0;
}

/**
 * gfs1_ri_update - attach rgrps to the super block
 *                  Stolen from libgfs2/super.c, but modified to handle gfs1.
 * @sdp:
 *
 * Given the rgrp index inode, link in all rgrps into the super block
 * and be sure that they can be read.
 *
 * Returns: 0 on success, -1 on failure.
 */
int gfs1_ri_update(struct gfs2_sbd *sdp, int fd, int *rgcount)
{
	struct rgrp_list *rgd;
	osi_list_t *tmp;
	int count1 = 0, count2 = 0;
	uint64_t errblock = 0;

	if (gfs1_rindex_read(sdp, fd, &count1))
	    goto fail;
	for (tmp = sdp->rglist.next; tmp != &sdp->rglist; tmp = tmp->next) {
		enum update_flags f;

		f = not_updated;
		rgd = osi_list_entry(tmp, struct rgrp_list, list);
		errblock = gfs2_rgrp_read(sdp, rgd);
		if (errblock)
			return errblock;
		else
			gfs2_rgrp_relse(rgd, f);
		count2++;
	}

	*rgcount = count1;
	if (count1 != count2)
		goto fail;

	return 0;

 fail:
	gfs2_rgrp_free(&sdp->rglist, not_updated);
	return -1;
}


/*
 * get_gfs_struct_info - get block type and structure length
 *
 * @block_type - pointer to integer to hold the block type
 * @struct_length - pointer to integet to hold the structure length
 *
 * returns: 0 if successful
 *          -1 if this isn't gfs metadata.
 */
int get_gfs_struct_info(char *buf, int *block_type, int *struct_len)
{
	struct gfs2_meta_header mh;

	*block_type = 0;
	*struct_len = sbd.bsize;

	gfs2_meta_header_in(&mh, buf);
	if (mh.mh_magic != GFS2_MAGIC)
		return -1;

	*block_type = mh.mh_type;

	switch (mh.mh_type) {
	case GFS2_METATYPE_SB:   /* 1 (superblock) */
		*struct_len = sizeof(struct gfs_sb);
		break;
	case GFS2_METATYPE_RG:   /* 2 (rsrc grp hdr) */
		*struct_len = sbd.bsize; /*sizeof(struct gfs_rgrp);*/
		break;
	case GFS2_METATYPE_RB:   /* 3 (rsrc grp bitblk) */
		*struct_len = sbd.bsize;
		break;
	case GFS2_METATYPE_DI:   /* 4 (disk inode) */
		*struct_len = sbd.bsize; /*sizeof(struct gfs_dinode);*/
		break;
	case GFS2_METATYPE_IN:   /* 5 (indir inode blklst) */
		*struct_len = sbd.bsize; /*sizeof(struct gfs_indirect);*/
		break;
	case GFS2_METATYPE_LF:   /* 6 (leaf dinode blklst) */
		*struct_len = sbd.bsize; /*sizeof(struct gfs_leaf);*/
		break;
	case GFS2_METATYPE_JD:   /* 7 (journal data) */
		*struct_len = sizeof(struct gfs2_meta_header);
		break;
	case GFS2_METATYPE_LH:   /* 8 (log header) */
		*struct_len = sizeof(struct gfs2_log_header);
		break;
	case GFS2_METATYPE_LD:   /* 9 (log descriptor) */
		*struct_len = sbd.bsize;
		break;
	case GFS2_METATYPE_EA:   /* 10 (extended attr hdr) */
		*struct_len = sbd.bsize;
		break;
	case GFS2_METATYPE_ED:   /* 11 (extended attr data) */
		*struct_len = sbd.bsize;
		break;
	default:
		*struct_len = sbd.bsize;
		break;
	}
	return 0;
}

/* Put out a warm, fuzzy message every second so the user     */
/* doesn't think we hung.  (This may take a long time).       */
/* We only check whether to report every one percent because  */
/* checking every block kills performance.  We only report    */
/* every second because we don't need 100 extra messages in   */
/* logs made from verbose mode.                               */
void warm_fuzzy_stuff(uint64_t block, int force, int save)
{
        static struct timeval tv;
        static uint32_t seconds = 0;
        
	last_reported_block = block;
	gettimeofday(&tv, NULL);
	if (!seconds)
		seconds = tv.tv_sec;
	if (force || tv.tv_sec - seconds) {
		static uint64_t percent;

		seconds = tv.tv_sec;
		if (last_fs_block) {
			printf("\r");
			if (save) {
				percent = (block * 100) / last_fs_block;
				printf("%" PRIu64 " metadata blocks (%"
				       PRIu64 "%%) processed, ", block,
				       percent);
			}
			if (total_out < 1024 * 1024)
				printf("%" PRIu64 " metadata blocks (%"
				       PRIu64 "KB) %s.    ",
				       blks_saved, total_out / 1024,
				       save?"saved":"restored");
			else
				printf("%" PRIu64 " metadata blocks (%"
				       PRIu64 "MB) %s.    ",
				       blks_saved, total_out / (1024*1024),
				       save?"saved":"restored");
			if (force)
				printf("\n");
			fflush(stdout);
		}
	}
}

int block_is_a_journal(void)
{
	int j;

	for (j = 0; j < journals_found; j++)
		if (block == journal_blocks[j])
			return TRUE;
	return FALSE;
}

int block_is_systemfile(void)
{
	return block_is_jindex() ||
		block_is_inum_file() ||
		block_is_statfs_file() ||
		block_is_quota_file() ||
		block_is_rindex() ||
		block_is_a_journal();
}

int save_block(int fd, int out_fd, uint64_t blk)
{
	int blktype, blklen, outsz;
	uint16_t trailing0;
	char *p;

	if (blk > last_fs_block) {
		fprintf(stderr, "\nWarning: bad block pointer ignored in "
			"block (block %llu (%llx))",
			(unsigned long long)block, (unsigned long long)block);
		return 0;
	}
	memset(savedata, 0, sizeof(struct saved_metablock));
	do_lseek(fd, blk * sbd.bsize);
	do_read(fd, savedata->buf, sbd.bsize); /* read in the block */

	/* If this isn't metadata and isn't a system file, we don't want it.
	   Note that we're checking "block" here rather than blk.  That's
	   because we want to know if the source inode's "block" is a system
	   inode, not the block within the inode "blk". They may or may not
	   be the same thing. */
	if (get_gfs_struct_info(savedata->buf, &blktype, &blklen) &&
	    !block_is_systemfile())
		return 0; /* Not metadata, and not system file, so skip it */
	trailing0 = 0;
	p = &savedata->buf[blklen - 1];
	while (*p=='\0' && trailing0 < sbd.bsize) {
		trailing0++;
		p--;
	}
	savedata->blk = cpu_to_be64(blk);
	do_write(out_fd, &savedata->blk, sizeof(savedata->blk));
	outsz = blklen - trailing0;
	savedata->siglen = cpu_to_be16(outsz);
	do_write(out_fd, &savedata->siglen, sizeof(savedata->siglen));
	do_write(out_fd, savedata->buf, outsz);
	total_out += sizeof(savedata->blk) + sizeof(savedata->siglen) + outsz;
	blks_saved++;
	return blktype;
}

/*
 * save_indirect_blocks - save all indirect blocks for the given buffer
 */
void save_indirect_blocks(int out_fd, osi_list_t *cur_list,
			  struct gfs2_buffer_head *mybh, int height, int hgt)
{
	uint64_t old_block = 0, indir_block;
	uint64_t *ptr;
	int head_size;
	struct gfs2_buffer_head *nbh;

	head_size = (hgt > 1 ?
		     sizeof(struct gfs2_meta_header) :
		     sizeof(struct gfs2_dinode));

	for (ptr = (uint64_t *)(mybh->b_data + head_size);
	     (char *)ptr < (mybh->b_data + sbd.bsize); ptr++) {
		if (!*ptr)
			continue;
		indir_block = be64_to_cpu(*ptr);
		if (indir_block == old_block)
			continue;
		old_block = indir_block;
		save_block(sbd.device_fd, out_fd, indir_block);
		if (height != hgt) { /* If not at max height */
			nbh = bread(&sbd, indir_block);
			osi_list_add_prev(&nbh->b_altlist,
					  cur_list);
			brelse(nbh, not_updated);
		}
	} /* for all data on the indirect block */
}

/*
 * save_inode_data - save off important data associated with an inode
 *
 * out_fd - destination file descriptor
 * block - block number of the inode to save the data for
 * 
 * For user files, we don't want anything except all the indirect block
 * pointers that reside on blocks on all but the highest height.
 *
 * For system files like statfs and inum, we want everything because they
 * may contain important clues and no user data.
 *
 * For file system journals, the "data" is a mixture of metadata and
 * journaled data.  We want all the metadata and none of the user data.
 */
void save_inode_data(int out_fd)
{
	uint32_t height;
	struct gfs2_inode *inode;
	osi_list_t metalist[GFS2_MAX_META_HEIGHT];
	osi_list_t *prev_list, *cur_list, *tmp;
	struct gfs2_buffer_head *metabh, *mybh;
	int i;
	char *buf;

	for (i = 0; i < GFS2_MAX_META_HEIGHT; i++)
		osi_list_init(&metalist[i]);
	buf = malloc(sbd.bsize);
	metabh = bread(&sbd, block);
	inode = inode_get(&sbd, metabh);
	height = inode->i_di.di_height;
	/* If this is a user inode, we don't follow to the file height.
	   We stop one level less.  That way we save off the indirect
	   pointer blocks but not the actual file contents. */
	if (height && !block_is_systemfile())
		height--;
	osi_list_add(&metabh->b_altlist, &metalist[0]);
        for (i = 1; i <= height; i++){
		prev_list = &metalist[i - 1];
		cur_list = &metalist[i];

		for (tmp = prev_list->next; tmp != prev_list; tmp = tmp->next){
			mybh = osi_list_entry(tmp, struct gfs2_buffer_head,
					      b_altlist);
			save_indirect_blocks(out_fd, cur_list, mybh,
					     height, i);
		} /* for blocks at that height */
	} /* for height */
	/* free metalists */
	for (i = 0; i < GFS2_MAX_META_HEIGHT; i++) {
		cur_list = &metalist[i];
		while (!osi_list_empty(cur_list)) {
			mybh = osi_list_entry(cur_list->next,
					    struct gfs2_buffer_head,
					    b_altlist);
			osi_list_del(&mybh->b_altlist);
		}
	}
	/* Process directory exhash inodes */
	if (S_ISDIR(inode->i_di.di_mode)) {
		if (inode->i_di.di_flags & GFS2_DIF_EXHASH) {
			save_indirect_blocks(out_fd, cur_list, metabh,
					     height, 0);
		}
	}
	if (inode->i_di.di_eattr) { /* if this inode has extended attributes */
		struct gfs2_ea_header ea;
		struct gfs2_meta_header mh;
		int e;

		metabh = bread(&sbd, inode->i_di.di_eattr);
		save_block(sbd.device_fd, out_fd, inode->i_di.di_eattr);
		gfs2_meta_header_in(&mh, metabh->b_data);
		if (mh.mh_magic == GFS2_MAGIC) {
			for (e = sizeof(struct gfs2_meta_header);
			     e < sbd.bsize; e += ea.ea_rec_len) {
				uint64_t blk, *b;
				int charoff;

				gfs2_ea_header_in(&ea, metabh->b_data + e);
				for (i = 0; i < ea.ea_num_ptrs; i++) {
					charoff = e + ea.ea_name_len +
						sizeof(struct gfs2_ea_header) +
						sizeof(uint64_t) - 1;
					charoff /= sizeof(uint64_t);
					b = (uint64_t *)(metabh->b_data);
					b += charoff + i;
					blk = be64_to_cpu(*b);
					save_block(sbd.device_fd, out_fd, blk);
				}
				if (!ea.ea_rec_len)
					break;
			}
		} else {
			fprintf(stderr, "\nWarning: corrupt extended attribute"
				" at block %llu (0x%llx) detected.\n",
				(unsigned long long)block,
				(unsigned long long)block);
		}
		brelse(metabh, not_updated);
	}
	inode_put(inode, not_updated);
	free(buf);
}

void get_journal_inode_blocks(void)
{
	int journal;
	struct gfs2_buffer_head *bh;

	journals_found = 0;
	memset(journal_blocks, 0, sizeof(journal_blocks));
	/* Save off all the journals--but only the metadata.
	 * This is confusing so I'll explain.  The journals contain important 
	 * metadata.  However, in gfs2 the journals are regular files within
	 * the system directory.  Since they're regular files, the blocks
	 * within the journals are considered data, not metadata.  Therefore,
	 * they won't have been saved by the code above.  We want to dump
	 * these blocks, but we have to be careful.  We only care about the
	 * journal blocks that look like metadata, and we need to not save
	 * journaled user data that may exist there as well. */
	for (journal = 0; ; journal++) { /* while journals exist */
		uint64_t jblock;
		int amt;
		struct gfs2_dinode jdi;
		struct gfs2_inode *j_inode = NULL;

		if (gfs1) {
			struct gfs_jindex ji;
			char jbuf[sizeof(struct gfs_jindex)];

			bh = bread(&sbd, sbd1->sb_jindex_di.no_addr);
			j_inode = inode_get(&sbd, bh);
			brelse(bh, not_updated);
			amt = gfs2_readi(j_inode, (void *)&jbuf,
					 journal * sizeof(struct gfs_jindex),
					 sizeof(struct gfs_jindex));
			if (!amt)
				break;
			gfs_jindex_in(&ji, jbuf);
			jblock = ji.ji_addr;
			gfs1_journal_size = ji.ji_nsegment * 16;
		} else {
			if (journal > indirect->ii[0].dirents - 3)
				break;
			jblock = indirect->ii[0].dirent[journal + 2].block;
			bh = bread(&sbd, jblock);
			j_inode = inode_get(&sbd, bh);
			gfs2_dinode_in(&jdi, bh->b_data);
			inode_put(j_inode, not_updated);
		}
		journal_blocks[journals_found++] = jblock;
	}
}

void savemeta(const char *out_fn, int saveoption)
{
	int out_fd;
	int slow;
	osi_list_t *tmp;
	uint64_t memreq;
	int rgcount;
	uint64_t jindex_block;
	struct gfs2_buffer_head *bh;

	slow = (saveoption == 1);
	sbd.md.journals = 1;

	if (!out_fn)
		out_fn = DFT_SAVE_FILE;
	out_fd = open(out_fn, O_RDWR | O_CREAT, 0644);
	if (out_fd < 0)
		die("Can't open %s: %s\n", out_fn, strerror(errno));

	if (ftruncate(out_fd, 0))
		die("Can't truncate %s: %s\n", out_fn, strerror(errno));
	savedata = malloc(sizeof(struct saved_metablock));
	if (!savedata)
		die("Can't allocate memory for the operation.\n");

	do_lseek(sbd.device_fd, 0);
	blks_saved = total_out = last_reported_block = 0;
	sbd.bsize = BUFSIZE;
	if (!slow) {
		int i;

		device_geometry(&sbd);
		fix_device_geometry(&sbd);
		osi_list_init(&sbd.rglist);
		osi_list_init(&sbd.buf_list);
		for(i = 0; i < BUF_HASH_SIZE; i++)
			osi_list_init(&sbd.buf_hash[i]);
		sbd.sd_sb.sb_bsize = GFS2_DEFAULT_BSIZE;
		compute_constants(&sbd);
		if(!gfs1 && read_sb(&sbd) < 0)
			slow = TRUE;
		else
			sbd.bsize = sbd.bsize = sbd.sd_sb.sb_bsize;
	}
	last_fs_block = lseek(sbd.device_fd, 0, SEEK_END) / sbd.bsize;
	printf("There are %" PRIu64 " blocks of %u bytes.\n",
	       last_fs_block, sbd.bsize);
	if (!slow) {
		if (gfs1) {
			sbd.md.riinode =
				gfs2_load_inode(&sbd,
						sbd1->sb_rindex_di.no_addr);
			jindex_block = sbd1->sb_jindex_di.no_addr;
		} else {
			sbd.master_dir =
				gfs2_load_inode(&sbd,
						sbd.sd_sb.sb_master_dir.no_addr);

			slow = gfs2_lookupi(sbd.master_dir, "rindex", 6, 
					    &sbd.md.riinode);
			jindex_block = masterblock("jindex");
		}
		bh = bread(&sbd, jindex_block);
		gfs2_dinode_in(&di, bh->b_data);
		if (!gfs1)
			do_dinode_extended(&di, bh->b_data);
		brelse(bh, not_updated);
	}
	if (!slow) {
		printf("Reading resource groups...");
		fflush(stdout);
		if (gfs1)
			slow = gfs1_ri_update(&sbd, 0, &rgcount);
		else
			slow = ri_update(&sbd, 0, &rgcount);
		printf("Done.\n\n");
		fflush(stdout);
	}
	if (!slow) {
		blocklist = gfs2_block_list_create(last_fs_block + 1, &memreq);
		if (!blocklist)
			slow = TRUE;
	}
	get_journal_inode_blocks();
	if (!slow) {
		/* Save off the superblock */
		save_block(sbd.device_fd, out_fd, 0x10 * (4096 / sbd.bsize));
		/* If this is gfs1, save off the rindex because it's not
		   part of the file system as it is in gfs2. */
		if (gfs1) {
			int j;

			block = sbd1->sb_rindex_di.no_addr;
			save_block(sbd.device_fd, out_fd, block);
			save_inode_data(out_fd);
			/* In GFS1, journals aren't part of the RG space */
			for (j = 0; j < journals_found; j++) {
				log_debug("Saving journal #%d\n", j + 1);
				for (block = journal_blocks[j];
				     block < journal_blocks[j] +
					     gfs1_journal_size;
				     block++)
					save_block(sbd.device_fd, out_fd, block);
			}
		}
		/* Walk through the resource groups saving everything within */
		for (tmp = sbd.rglist.next; tmp != &sbd.rglist;
		     tmp = tmp->next){
			struct rgrp_list *rgd;
			int i, first;

			rgd = osi_list_entry(tmp, struct rgrp_list, list);
			slow = gfs2_rgrp_read(&sbd, rgd);
			if (slow)
				continue;
			log_debug("RG at %"PRIu64" is %u long\n",
				  rgd->ri.ri_addr, rgd->ri.ri_length);
			for (i = 0; i < rgd->ri.ri_length; i++) {
				if(gfs2_block_set(blocklist,
						  rgd->ri.ri_addr + i,
						  gfs2_meta_other))
					break;
			}
			first = 1;
			/* Save off the rg and bitmaps */
			for (block = rgd->ri.ri_addr;
			     block < rgd->ri.ri_data0; block++) {
				warm_fuzzy_stuff(block, FALSE, TRUE);
				save_block(sbd.device_fd, out_fd, block);
			}
			/* Save off the other metadata: inodes, etc. */
			if (saveoption != 2) {
				while (!gfs2_next_rg_meta(rgd, &block, first)) {
					int blktype;

					warm_fuzzy_stuff(block, FALSE, TRUE);
					blktype = save_block(sbd.device_fd,
							     out_fd, block);
					if (blktype == GFS2_METATYPE_DI)
						save_inode_data(out_fd);
					first = 0;
				}
			}
			gfs2_rgrp_relse(rgd, not_updated);
		}
	}
	if (slow) {
		for (block = 0; block < last_fs_block; block++) {
			save_block(sbd.device_fd, out_fd, block);
		}
	}
	/* Clean up */
	if (blocklist)
		gfs2_block_list_destroy(blocklist);
	/* There may be a gap between end of file system and end of device */
	/* so we tell the user that we've processed everything. */
	block = last_fs_block;
	warm_fuzzy_stuff(block, TRUE, TRUE);
	printf("\nMetadata saved to file %s.\n", out_fn);
	free(savedata);
	close(out_fd);
	close(sbd.device_fd);
	exit(0);
}

int restore_data(int fd, int in_fd, int printblocksonly)
{
	size_t rs;
	uint64_t buf64, writes = 0;
	uint16_t buf16;
	int first = 1;

	if (!printblocksonly)
		do_lseek(fd, 0);
	blks_saved = total_out = 0;
	last_fs_block = 0;
	while (TRUE) {
		memset(savedata, 0, sizeof(struct saved_metablock));
		rs = read(in_fd, &buf64, sizeof(uint64_t));
		if (!rs)
			break;
		if (rs != sizeof(uint64_t)) {
			fprintf(stderr, "Error reading from file.\n");
			return -1;
		}
		total_out += sbd.bsize;
		savedata->blk = be64_to_cpu(buf64);
		if (!printblocksonly &&
		    last_fs_block && savedata->blk >= last_fs_block) {
			fprintf(stderr, "Error: File system is too small to "
				"restore this metadata.\n");
			fprintf(stderr, "File system is %" PRIu64 " blocks, ",
				last_fs_block);
			fprintf(stderr, "Restore block = %" PRIu64 "\n",
				savedata->blk);
			return -1;
		}
		rs = read(in_fd, &buf16, sizeof(uint16_t));
		savedata->siglen = be16_to_cpu(buf16);
		if (savedata->siglen > 0 &&
		    savedata->siglen <= sizeof(savedata->buf)) {
			do_read(in_fd, savedata->buf, savedata->siglen);
			if (first) {
				gfs2_sb_in(&sbd.sd_sb, savedata->buf);
				sbd1 = (struct gfs_sb *)&sbd.sd_sb;
				if (sbd1->sb_fs_format == GFS_FORMAT_FS &&
				    sbd1->sb_header.mh_type ==
				    GFS_METATYPE_SB &&
				    sbd1->sb_header.mh_format ==
				    GFS_FORMAT_SB &&
				    sbd1->sb_multihost_format ==
				    GFS_FORMAT_MULTI)
					;
				else if (check_sb(&sbd.sd_sb)) {
					fprintf(stderr,"Error: Invalid superblock data.\n");
					return -1;
				}
				sbd.bsize = sbd.sd_sb.sb_bsize;
				if (!printblocksonly) {
					last_fs_block =
						lseek(fd, 0, SEEK_END) /
						sbd.bsize;
					printf("There are %" PRIu64 " blocks of " \
					       "%u bytes in the destination" \
					       " file system.\n\n",
					       last_fs_block, sbd.bsize);
				}
				first = 0;
			}
			if (printblocksonly) {
				print_gfs2("%d (l=0x%x): ", blks_saved,
					   savedata->siglen);
				block = savedata->blk;
				display_block_type(savedata->buf, TRUE);
			} else {
				warm_fuzzy_stuff(savedata->blk, FALSE, FALSE);
				if (savedata->blk >= last_fs_block) {
					printf("Out of space on the destination "
					       "device; quitting.\n");
					break;
				}
				do_lseek(fd, savedata->blk * sbd.bsize);
				do_write(fd, savedata->buf, sbd.bsize);
				writes++;
			}
			blks_saved++;
		} else {
			fprintf(stderr, "Bad record length: %d for #%"
				PRIu64".\n", savedata->siglen, savedata->blk);
			return -1;
		}
	}
	if (!printblocksonly)
		warm_fuzzy_stuff(savedata->blk, TRUE, FALSE);
	return 0;
}

void complain(const char *complaint)
{
	fprintf(stderr, "%s\n", complaint);
	die("Format is: \ngfs2_edit restoremeta <file to restore> "
	    "<dest file system>\n");
}

void restoremeta(const char *in_fn, const char *out_device,
		 int printblocksonly)
{
	int in_fd, error;

	termlines = 0;
	if (!in_fn)
		complain("No source file specified.");
	if (!printblocksonly && !out_device)
		complain("No destination file system specified.");
	in_fd = open(in_fn, O_RDONLY);
	if (in_fd < 0)
		die("Can't open source file %s: %s\n",
		    in_fn, strerror(errno));

	if (!printblocksonly) {
		sbd.device_fd = open(out_device, O_RDWR);
		if (sbd.device_fd < 0)
			die("Can't open destination file system %s: %s\n",
			    out_device, strerror(errno));
	}
	savedata = malloc(sizeof(struct saved_metablock));
	if (!savedata)
		die("Can't allocate memory for the restore operation.\n");

	blks_saved = 0;
	error = restore_data(sbd.device_fd, in_fd, printblocksonly);
	printf("File %s %s %s.\n", in_fn,
	       (printblocksonly ? "print" : "restore"),
	       (error ? "error" : "successful"));
	free(savedata);
	close(in_fd);
	if (!printblocksonly)
		close(sbd.device_fd);

	exit(0);
}
