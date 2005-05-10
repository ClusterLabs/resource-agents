/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "gfs2_mkfs.h"

void
build_master(struct gfs2_sbd *sdp)
{
	uint64_t bn;
	struct gfs2_inum inum;
	struct buffer_head *bh;

	bn = dinode_alloc(sdp);

	inum.no_formal_ino = sdp->next_inum++;
	inum.no_addr = bn;

	bh = init_dinode(sdp, &inum, S_IFDIR | 0700, GFS2_DIF_SYSTEM, &inum);

	sdp->master_dir = inode_get(sdp, bh);

	if (sdp->debug) {
		printf("\nMaster dir:\n");
		gfs2_dinode_print(&sdp->master_dir->i_di);
	}
}

void
build_sb(struct gfs2_sbd *sdp)
{
	unsigned int x;
	struct buffer_head *bh;
	struct gfs2_sb sb;

	/* Zero out the beginning of the device up to the superblock */
	for (x = 0; x < sdp->sb_addr; x++) {
		bh = bget(sdp, x);
		bh->b_uninit = TRUE;
		brelse(bh);
	}

	memset(&sb, 0, sizeof(struct gfs2_sb));
	sb.sb_header.mh_magic = GFS2_MAGIC;
	sb.sb_header.mh_type = GFS2_METATYPE_SB;
	sb.sb_header.mh_blkno = sdp->sb_addr;
	sb.sb_header.mh_format = GFS2_FORMAT_SB;
	sb.sb_fs_format = GFS2_FORMAT_FS;
	sb.sb_multihost_format = GFS2_FORMAT_MULTI;
	sb.sb_bsize = sdp->bsize;
	sb.sb_bsize_shift = sdp->bsize_shift;
	sb.sb_master_dir = sdp->master_dir->i_di.di_num;
	strcpy(sb.sb_lockproto, sdp->lockproto);
	strcpy(sb.sb_locktable, sdp->locktable);

	bh = bget(sdp, sdp->sb_addr);
	gfs2_sb_out(&sb, bh->b_data);
	brelse(bh);

	if (sdp->debug) {
		printf("\nSuper Block:\n");
		gfs2_sb_print(&sb);
	}
}

static void
build_journal(struct gfs2_inode *jindex, unsigned int j)
{
	struct gfs2_sbd *sdp = jindex->i_sbd;
	char name[256];
	struct gfs2_inode *ip;
	struct gfs2_log_header lh;
	unsigned int blocks = sdp->jsize << 20 >> sdp->bsize_shift;
	unsigned int x;
	uint64_t seq = RANDOM(blocks);
	uint32_t hash;

	sprintf(name, "journal%u", j);
	ip = createi(jindex, name, S_IFREG | 0600,
		     GFS2_DIF_SYSTEM);

	memset(&lh, 0, sizeof(struct gfs2_log_header));
	lh.lh_header.mh_magic = GFS2_MAGIC;
	lh.lh_header.mh_type = GFS2_METATYPE_LH;
	lh.lh_header.mh_format = GFS2_FORMAT_LH;
	lh.lh_flags = GFS2_LOG_HEAD_UNMOUNT;

	for (x = 0; x < blocks; x++) {
		struct buffer_head *bh = get_file_buf(ip, ip->i_di.di_size >> sdp->bsize_shift);
		if (!bh)
			die("build_journals\n");

		lh.lh_header.mh_blkno = bh->b_blocknr;
		lh.lh_sequence = seq;
		lh.lh_blkno = x;
		gfs2_log_header_out(&lh, bh->b_data);
		hash = gfs2_disk_hash(bh->b_data, sizeof(struct gfs2_log_header));
		((struct gfs2_log_header *)bh->b_data)->lh_hash = cpu_to_gfs2_32(hash);

		brelse(bh);

		if (++seq == blocks)
			seq = 0;
	}

	if (sdp->debug) {
		printf("\nJournal %u:\n", j);
		gfs2_dinode_print(&ip->i_di);
	}

	inode_put(ip);
}

void
build_jindex(struct gfs2_sbd *sdp)
{
	struct gfs2_inode *jindex;
	unsigned int j;

	jindex = createi(sdp->master_dir, "jindex", S_IFDIR | 0700,
			 GFS2_DIF_SYSTEM);

	for (j = 0; j < sdp->journals; j++)
		build_journal(jindex, j);

	if (sdp->debug) {
		printf("\nJindex:\n");
		gfs2_dinode_print(&jindex->i_di);
	}

	inode_put(jindex);
}

static void
build_inum_range(struct gfs2_inode *per_node, unsigned int j)
{
	struct gfs2_sbd *sdp = per_node->i_sbd;
	char name[256];
	struct gfs2_inode *ip;

	sprintf(name, "inum_range%u", j);
	ip = createi(per_node, name, S_IFREG | 0600,
		     GFS2_DIF_SYSTEM | GFS2_DIF_JDATA);
	ip->i_di.di_size = sizeof(struct gfs2_inum_range);

	if (sdp->debug) {
		printf("\nInum Range %u:\n", j);
		gfs2_dinode_print(&ip->i_di);
	}

	inode_put(ip);
}

static void
build_statfs_change(struct gfs2_inode *per_node, unsigned int j)
{
	struct gfs2_sbd *sdp = per_node->i_sbd;
	char name[256];
	struct gfs2_inode *ip;

	sprintf(name, "statfs_change%u", j);
	ip = createi(per_node, name, S_IFREG | 0600,
		     GFS2_DIF_SYSTEM | GFS2_DIF_JDATA);
	ip->i_di.di_size = sizeof(struct gfs2_statfs_change);

	if (sdp->debug) {
		printf("\nStatFS Change %u:\n", j);
		gfs2_dinode_print(&ip->i_di);
	}

	inode_put(ip);
}

static void
build_unlinked_tag(struct gfs2_inode *per_node, unsigned int j)
{
	struct gfs2_sbd *sdp = per_node->i_sbd;
	struct gfs2_meta_header mh;
	char name[256];
	struct gfs2_inode *ip;
	unsigned int blocks = sdp->utsize << (20 - sdp->bsize_shift);
	unsigned int x;

	memset(&mh, 0, sizeof(struct gfs2_meta_header));
	mh.mh_magic = GFS2_MAGIC;
	mh.mh_type = GFS2_METATYPE_UT;
	mh.mh_format = GFS2_FORMAT_UT;

	sprintf(name, "unlinked_tag%u", j);
	ip = createi(per_node, name, S_IFREG | 0600,
		     GFS2_DIF_SYSTEM);

	for (x = 0; x < blocks; x++) {
		struct buffer_head *bh = get_file_buf(ip, ip->i_di.di_size >> sdp->bsize_shift);
		if (!bh)
			die("build_unlinked_tag\n");

		mh.mh_blkno = bh->b_blocknr;
		gfs2_meta_header_out(&mh, bh->b_data);

		brelse(bh);
	}

	if (sdp->debug) {
		printf("\nUnlinked Tag %u:\n", j);
		gfs2_dinode_print(&ip->i_di);
	}

	inode_put(ip);
}

static void
build_quota_change(struct gfs2_inode *per_node, unsigned int j)
{
	struct gfs2_sbd *sdp = per_node->i_sbd;
	struct gfs2_meta_header mh;
	char name[256];
	struct gfs2_inode *ip;
	unsigned int blocks = sdp->qcsize << (20 - sdp->bsize_shift);
	unsigned int x;

	memset(&mh, 0, sizeof(struct gfs2_meta_header));
	mh.mh_magic = GFS2_MAGIC;
	mh.mh_type = GFS2_METATYPE_QC;
	mh.mh_format = GFS2_FORMAT_QC;

	sprintf(name, "quota_change%u", j);
	ip = createi(per_node, name, S_IFREG | 0600,
		     GFS2_DIF_SYSTEM);

	for (x = 0; x < blocks; x++) {
		struct buffer_head *bh = get_file_buf(ip, ip->i_di.di_size >> sdp->bsize_shift);
		if (!bh)
			die("build_quota_change\n");

		mh.mh_blkno = bh->b_blocknr;
		gfs2_meta_header_out(&mh, bh->b_data);

		brelse(bh);
	}

	if (sdp->debug) {
		printf("\nQuota Change %u:\n", j);
		gfs2_dinode_print(&ip->i_di);
	}

	inode_put(ip);
}

void
build_per_node(struct gfs2_sbd *sdp)
{
	struct gfs2_inode *per_node;
	unsigned int j;

	per_node = createi(sdp->master_dir, "per_node", S_IFDIR | 0700,
			   GFS2_DIF_SYSTEM);

	for (j = 0; j < sdp->journals; j++) {
		build_inum_range(per_node, j);
		build_statfs_change(per_node, j);
		build_unlinked_tag(per_node, j);
		build_quota_change(per_node, j);
	}

	if (sdp->debug) {
		printf("\nper_node:\n");
		gfs2_dinode_print(&per_node->i_di);
	}

	inode_put(per_node);
}

void
build_inum(struct gfs2_sbd *sdp)
{
	struct gfs2_inode *ip;

	ip = createi(sdp->master_dir, "inum", S_IFREG | 0600,
		     GFS2_DIF_SYSTEM | GFS2_DIF_JDATA);

	if (sdp->debug) {
		printf("\nInum Inode:\n");
		gfs2_dinode_print(&ip->i_di);
	}

	sdp->inum_inode = ip;
}

void
build_statfs(struct gfs2_sbd *sdp)
{
	struct gfs2_inode *ip;

	ip = createi(sdp->master_dir, "statfs", S_IFREG | 0600,
		     GFS2_DIF_SYSTEM | GFS2_DIF_JDATA);

	if (sdp->debug) {
		printf("\nStatFS Inode:\n");
		gfs2_dinode_print(&ip->i_di);
	}

	sdp->statfs_inode = ip;
}

void
build_rindex(struct gfs2_sbd *sdp)
{
	struct gfs2_inode *ip;
	osi_list_t *tmp, *head;
	struct rgrp_list *rl;
	char buf[sizeof(struct gfs2_rindex)];
	int count;

	ip = createi(sdp->master_dir, "rindex", S_IFREG | 0600,
		     GFS2_DIF_SYSTEM | GFS2_DIF_JDATA);
	ip->i_di.di_payload_format = GFS2_FORMAT_RI;

	for (head = &sdp->rglist, tmp = head->next;
	     tmp != head;
	     tmp = tmp->next) {
		rl = osi_list_entry(tmp, struct rgrp_list, list);

		gfs2_rindex_out(&rl->ri, buf);

		count = writei(ip, buf, ip->i_di.di_size, sizeof(struct gfs2_rindex));
		if (count != sizeof(struct gfs2_rindex))
			die("build_rindex\n");
	}

	if (sdp->debug) {
		printf("\nResource Index:\n");
		gfs2_dinode_print(&ip->i_di);
	}

	inode_put(ip);
}

void
build_quota(struct gfs2_sbd *sdp)
{
	struct gfs2_inode *ip;
	struct gfs2_quota qu;
	char buf[sizeof(struct gfs2_quota)];
	int count;

	ip = createi(sdp->master_dir, "quota", S_IFREG | 0600,
		     GFS2_DIF_SYSTEM | GFS2_DIF_JDATA);
	ip->i_di.di_payload_format = GFS2_FORMAT_QU;

	memset(&qu, 0, sizeof(struct gfs2_quota));
	qu.qu_value = 1;
	gfs2_quota_out(&qu, buf);

	count = writei(ip, buf, ip->i_di.di_size, sizeof(struct gfs2_quota));
	if (count != sizeof(struct gfs2_quota))
		die("do_init (2)\n");
	count = writei(ip, buf, ip->i_di.di_size, sizeof(struct gfs2_quota));
	if (count != sizeof(struct gfs2_quota))
		die("do_init (3)\n");

	if (sdp->debug) {
		printf("\nRoot quota:\n");
		gfs2_quota_print(&qu);
	}

	inode_put(ip);
}

void
build_root(struct gfs2_sbd *sdp)
{
	struct gfs2_inode *ip;

	ip = createi(sdp->master_dir, "root", S_IFDIR | 0755, 0);

	if (sdp->debug) {
		printf("\nRoot directory:\n");
		gfs2_dinode_print(&ip->i_di);
	}

	inode_put(ip);
}

void
do_init(struct gfs2_sbd *sdp)
{
	{
		struct gfs2_inode *ip = sdp->inum_inode;
		uint64_t buf;
		int count;

		buf = cpu_to_gfs2_64(sdp->next_inum);
		count = writei(ip, &buf, 0, sizeof(uint64_t));
		if (count != sizeof(uint64_t))
			die("do_init (1)\n");

		if (sdp->debug)
			printf("\nNext Inum: %"PRIu64"\n",
			       sdp->next_inum);
	}

	{
		struct gfs2_inode *ip = sdp->statfs_inode;
		struct gfs2_statfs_change sc;
		char buf[sizeof(struct gfs2_statfs_change)];
		int count;

		sc.sc_total = sdp->blks_total;
		sc.sc_free = sdp->blks_total - sdp->blks_alloced;
		sc.sc_dinodes = sdp->dinodes_alloced;

		gfs2_statfs_change_out(&sc, buf);
		count = writei(ip, buf, 0, sizeof(struct gfs2_statfs_change));
		if (count != sizeof(struct gfs2_statfs_change))
			die("do_init (2)\n");

		if (sdp->debug) {
			printf("\nStatfs:\n");
			gfs2_statfs_change_print(&sc);
		}
	}
}


