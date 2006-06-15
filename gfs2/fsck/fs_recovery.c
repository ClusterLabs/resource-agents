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

#include <inttypes.h>
#include <linux_endian.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "libgfs2.h"
#include "util.h"
#include "fs_recovery.h"

#define RANDOM(values) ((values) * (random() / (RAND_MAX + 1.0)))

/*
 * reconstruct_single_journal - write a fresh journal
 * @sdp: superblock
 * @jnum: journal number
 *
 * This function will write a fresh journal over the top of
 * the previous journal.  All journal information is lost.  This
 * process is basically stolen from write_journals() in the mkfs code.
 *
 * Returns: -1 on error, 0 otherwise
 */
static int reconstruct_single_journal(struct gfs2_sbd *sdp, int jnum){
	struct gfs2_log_header	lh;
	unsigned int blocks;
	struct gfs2_inode *ip = sdp->md.journal[jnum];
	uint64_t seq;
	uint64_t dblock;
	uint32_t hash, extlen;
	unsigned int x;
	int new = 0;

	blocks = ip->i_di.di_blocks;
	srandom(time(NULL));
	seq = RANDOM(blocks);

	log_info("Clearing journal %d\n", jnum);

	memset(&lh, 0, sizeof(struct gfs2_log_header));
	lh.lh_header.mh_magic = GFS2_MAGIC;
	lh.lh_header.mh_type = GFS2_METATYPE_LH;
	lh.lh_header.mh_format = GFS2_FORMAT_LH;
	lh.lh_flags = GFS2_LOG_HEAD_UNMOUNT;

	for (x = 0; x < blocks; x++) {
		struct gfs2_buffer_head *bh;

		block_map(ip, x, &new, &dblock, &extlen);
		bh = bread(sdp, dblock);
		if (!bh) {
			log_err("Unable to read journal block at %" PRIu64
					" (0x%" PRIx64")\n", dblock, dblock);
			return -1;
		}

		lh.lh_sequence = seq;
		lh.lh_blkno = x;
		gfs2_log_header_out(&lh, bh->b_data);
		hash = gfs2_disk_hash(bh->b_data, sizeof(struct gfs2_log_header));
		((struct gfs2_log_header *)bh->b_data)->lh_hash = cpu_to_be32(hash);
		brelse(bh, updated);

		if (++seq == blocks)
			seq = 0;
	}

	return 0;
}


/*
 * reconstruct_journals - write fresh journals
 * sdp: the super block
 *
 * FIXME: it would be nice to get this to actually replay the journals
 * - there should be a flag to the fsck to enable/disable this
 * feature, and the fsck should probably fall back to clearing the
 * journal if an inconsitancy is found, but only for the bad journal
 *
 * Returns: 0 on success, -1 on failure
 */
int reconstruct_journals(struct gfs2_sbd *sdp){
	int i;

	log_notice("Clearing journals (this may take a while)");
	for(i=0; i < sdp->md.journals; i++) {
		/* Journal replay seems to have slowed down quite a bit in
		 * the gfs2_fsck */
		if((i % 2) == 0)
			log_at_notice(".");
		if(reconstruct_single_journal(sdp, i))
			return -1;
	}
	log_notice("\nJournals cleared.\n");
	return 0;
}
