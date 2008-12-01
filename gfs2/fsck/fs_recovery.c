#include <errno.h>
#include <inttypes.h>
#include <linux_endian.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "fsck.h"
#include "fs_recovery.h"
#include "libgfs2.h"
#include "util.h"

unsigned int sd_found_jblocks = 0, sd_replayed_jblocks = 0;
unsigned int sd_found_metablocks = 0, sd_replayed_metablocks = 0;
unsigned int sd_found_revokes = 0;
osi_list_t sd_revoke_list;
unsigned int sd_replay_tail;

struct gfs2_revoke_replay {
	osi_list_t rr_list;
	uint64_t rr_blkno;
	unsigned int rr_where;
};

int gfs2_revoke_add(struct gfs2_sbd *sdp, uint64_t blkno, unsigned int where)
{
	osi_list_t *tmp, *head = &sd_revoke_list;
	struct gfs2_revoke_replay *rr;
	int found = 0;

	osi_list_foreach(tmp, head) {
		rr = osi_list_entry(tmp, struct gfs2_revoke_replay, rr_list);
		if (rr->rr_blkno == blkno) {
			found = 1;
			break;
		}
	}

	if (found) {
		rr->rr_where = where;
		return 0;
	}

	rr = malloc(sizeof(struct gfs2_revoke_replay));
	if (!rr)
		return -ENOMEM;

	rr->rr_blkno = blkno;
	rr->rr_where = where;
	osi_list_add(&rr->rr_list, head);
	return 1;
}

int gfs2_revoke_check(struct gfs2_sbd *sdp, uint64_t blkno, unsigned int where)
{
	osi_list_t *tmp;
	struct gfs2_revoke_replay *rr;
	int wrap, a, b, revoke;
	int found = 0;

	osi_list_foreach(tmp, &sd_revoke_list) {
		rr = osi_list_entry(tmp, struct gfs2_revoke_replay, rr_list);
		if (rr->rr_blkno == blkno) {
			found = 1;
			break;
		}
	}

	if (!found)
		return 0;

	wrap = (rr->rr_where < sd_replay_tail);
	a = (sd_replay_tail < where);
	b = (where < rr->rr_where);
	revoke = (wrap) ? (a || b) : (a && b);
	return revoke;
}

void gfs2_revoke_clean(struct gfs2_sbd *sdp)
{
	osi_list_t *head = &sd_revoke_list;
	struct gfs2_revoke_replay *rr;

	while (!osi_list_empty(head)) {
		rr = osi_list_entry(head->next, struct gfs2_revoke_replay, rr_list);
		osi_list_del(&rr->rr_list);
		free(rr);
	}
}

static int buf_lo_scan_elements(struct gfs2_inode *ip, unsigned int start,
				struct gfs2_log_descriptor *ld, __be64 *ptr,
				int pass)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	unsigned int blks = be32_to_cpu(ld->ld_data1);
	struct gfs2_buffer_head *bh_log, *bh_ip;
	uint64_t blkno;
	int error = 0;
	enum update_flags if_modified;

	if (pass != 1 || be32_to_cpu(ld->ld_type) != GFS2_LOG_DESC_METADATA)
		return 0;

	gfs2_replay_incr_blk(ip, &start);

	for (; blks; gfs2_replay_incr_blk(ip, &start), blks--) {
		uint32_t check_magic;

		sd_found_metablocks++;

		blkno = be64_to_cpu(*ptr);
		ptr++;
		if (gfs2_revoke_check(sdp, blkno, start))
			continue;

		error = gfs2_replay_read_block(ip, start, &bh_log);
		if (error)
			return error;

		bh_ip = bget(&sdp->buf_list, blkno);
		memcpy(bh_ip->b_data, bh_log->b_data, sdp->bsize);

		check_magic = ((struct gfs2_meta_header *)
			       (bh_ip->b_data))->mh_magic;
		check_magic = be32_to_cpu(check_magic);
		if (check_magic != GFS2_MAGIC) {
			if_modified = not_updated;
			error = -EIO;
		} else
			if_modified = updated;

		brelse(bh_log, not_updated);
		brelse(bh_ip, if_modified);
		if (error)
			break;

		sd_replayed_metablocks++;
	}
	return error;
}

static int revoke_lo_scan_elements(struct gfs2_inode *ip, unsigned int start,
				   struct gfs2_log_descriptor *ld, __be64 *ptr,
				   int pass)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	unsigned int blks = be32_to_cpu(ld->ld_length);
	unsigned int revokes = be32_to_cpu(ld->ld_data1);
	struct gfs2_buffer_head *bh;
	unsigned int offset;
	uint64_t blkno;
	int first = 1;
	int error;

	if (pass != 0 || be32_to_cpu(ld->ld_type) != GFS2_LOG_DESC_REVOKE)
		return 0;

	offset = sizeof(struct gfs2_log_descriptor);

	for (; blks; gfs2_replay_incr_blk(ip, &start), blks--) {
		error = gfs2_replay_read_block(ip, start, &bh);
		if (error)
			return error;

		if (!first) {
			if (gfs2_check_meta(bh, GFS2_METATYPE_LB))
				continue;
		}
		while (offset + sizeof(uint64_t) <= sdp->sd_sb.sb_bsize) {
			blkno = be64_to_cpu(*(__be64 *)(bh->b_data + offset));
			error = gfs2_revoke_add(sdp, blkno, start);
			if (error < 0)
				return error;
			else if (error)
				sd_found_revokes++;

			if (!--revokes)
				break;
			offset += sizeof(uint64_t);
		}

		brelse(bh, updated);
		offset = sizeof(struct gfs2_meta_header);
		first = 0;
	}
	return 0;
}

static int databuf_lo_scan_elements(struct gfs2_inode *ip, unsigned int start,
				    struct gfs2_log_descriptor *ld,
				    __be64 *ptr, int pass)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	unsigned int blks = be32_to_cpu(ld->ld_data1);
	struct gfs2_buffer_head *bh_log, *bh_ip;
	uint64_t blkno;
	uint64_t esc;
	int error = 0;

	if (pass != 1 || be32_to_cpu(ld->ld_type) != GFS2_LOG_DESC_JDATA)
		return 0;

	gfs2_replay_incr_blk(ip, &start);
	for (; blks; gfs2_replay_incr_blk(ip, &start), blks--) {
		blkno = be64_to_cpu(*ptr);
		ptr++;
		esc = be64_to_cpu(*ptr);
		ptr++;

		sd_found_jblocks++;

		if (gfs2_revoke_check(sdp, blkno, start))
			continue;

		error = gfs2_replay_read_block(ip, start, &bh_log);
		if (error)
			return error;

		bh_ip = bget(&sdp->buf_list, blkno);
		memcpy(bh_ip->b_data, bh_log->b_data, sdp->bsize);

		/* Unescape */
		if (esc) {
			__be32 *eptr = (__be32 *)bh_ip->b_data;
			*eptr = cpu_to_be32(GFS2_MAGIC);
		}

		brelse(bh_log, not_updated);
		brelse(bh_ip, updated);

		sd_replayed_jblocks++;
	}
	return error;
}

/**
 * foreach_descriptor - go through the active part of the log
 * @ip: the journal incore inode
 * @start: the first log header in the active region
 * @end: the last log header (don't process the contents of this entry))
 *
 * Call a given function once for every log descriptor in the active
 * portion of the log.
 *
 * Returns: errno
 */

int foreach_descriptor(struct gfs2_inode *ip, unsigned int start,
		       unsigned int end, int pass)
{
	struct gfs2_buffer_head *bh;
	struct gfs2_log_descriptor *ld;
	int error = 0;
	uint32_t length;
	__be64 *ptr;
	unsigned int offset = sizeof(struct gfs2_log_descriptor);
	offset += sizeof(__be64) - 1;
	offset &= ~(sizeof(__be64) - 1);

	while (start != end) {
		uint32_t check_magic;

		error = gfs2_replay_read_block(ip, start, &bh);
		if (error)
			return error;
		check_magic = ((struct gfs2_meta_header *)
			       (bh->b_data))->mh_magic;
		check_magic = be32_to_cpu(check_magic);
		if (check_magic != GFS2_MAGIC) {
			brelse(bh, updated);
			return -EIO;
		}
		ld = (struct gfs2_log_descriptor *)bh->b_data;
		length = be32_to_cpu(ld->ld_length);

		if (be32_to_cpu(ld->ld_header.mh_type) == GFS2_METATYPE_LH) {
			struct gfs2_log_header lh;

			error = get_log_header(ip, start, &lh);
			if (!error) {
				gfs2_replay_incr_blk(ip, &start);
				brelse(bh, updated);
				continue;
			}
			if (error == 1)
				error = -EIO;
			brelse(bh, updated);
			return error;
		} else if (gfs2_check_meta(bh, GFS2_METATYPE_LD)) {
			brelse(bh, updated);
			return -EIO;
		}
		ptr = (__be64 *)(bh->b_data + offset);
		error = databuf_lo_scan_elements(ip, start, ld, ptr, pass);
		if (error) {
			brelse(bh, updated);
			return error;
		}
		error = buf_lo_scan_elements(ip, start, ld, ptr, pass);
		if (error) {
			brelse(bh, updated);
			return error;
		}
		error = revoke_lo_scan_elements(ip, start, ld, ptr, pass);
		if (error) {
			brelse(bh, updated);
			return error;
		}

		while (length--)
			gfs2_replay_incr_blk(ip, &start);

		brelse(bh, updated);
	}

	return 0;
}

/**
 * gfs2_recover_journal - recovery a given journal
 * @ip: the journal incore inode
 *
 * Acquire the journal's lock, check to see if the journal is clean, and
 * do recovery if necessary.
 *
 * Returns: errno
 */

int gfs2_recover_journal(struct gfs2_inode *ip, int j)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_log_header head;
	unsigned int pass;
	int error;

	log_info("jid=%u: Looking at journal...\n", j);

	osi_list_init(&sd_revoke_list);
	error = gfs2_find_jhead(ip, &head);
	if (error)
		goto out;

	if (head.lh_flags & GFS2_LOG_HEAD_UNMOUNT) {
		log_info("jid=%u: Journal is clean.\n", j);
		return 0;
	}
	if (query(&opts, "\nJournal #%d (\"journal%d\") is dirty.  Okay to replay it? (y/n)",
		    j+1, j)) {
		log_info("jid=%u: Replaying journal...\n", j);

		sd_found_jblocks = sd_replayed_jblocks = 0;
		sd_found_metablocks = sd_replayed_metablocks = 0;
		sd_found_revokes = 0;
		sd_replay_tail = head.lh_tail;
		for (pass = 0; pass < 2; pass++) {
			error = foreach_descriptor(ip, head.lh_tail,
						   head.lh_blkno, pass);
			if (error)
				goto out;
		}
		log_info("jid=%u: Found %u revoke tags\n", j,
			 sd_found_revokes);
		gfs2_revoke_clean(sdp);
		error = clean_journal(ip, &head);
		if (error)
			goto out;
		log_err("jid=%u: Replayed %u of %u journaled data blocks\n",
			j, sd_replayed_jblocks, sd_found_jblocks);
		log_err("jid=%u: Replayed %u of %u metadata blocks\n",
			j, sd_replayed_metablocks, sd_found_metablocks);
	} else {
		if (query(&opts, "Do you want to clear the dirty journal instead? (y/n)")) {
			write_journal(sdp, sdp->md.journal[j], j,
				      sdp->md.journal[j]->i_di.di_size /
				      sdp->sd_sb.sb_bsize);
			
		} else
			log_err("jid=%u: Dirty journal not replayed or cleared.\n", j);
	}

out:
	log_info("jid=%u: %s\n", j, (error) ? "Failed" : "Done");
	return error;
}

/*
 * replay_journals - replay the journals
 * sdp: the super block
 *
 * There should be a flag to the fsck to enable/disable this
 * feature.  The fsck falls back to clearing the journal if an 
 * inconsistency is found, but only for the bad journal.
 *
 * Returns: 0 on success, -1 on failure
 */
int replay_journals(struct gfs2_sbd *sdp){
	int i;

	log_notice("Recovering journals (this may take a while)");

	/* Get master dinode */
	sdp->master_dir = gfs2_load_inode(sdp,
					  sdp->sd_sb.sb_master_dir.no_addr);
	gfs2_lookupi(sdp->master_dir, "jindex", 6, &sdp->md.jiinode);

	/* read in the journal index data */
	if (ji_update(sdp)){
		log_err("Unable to read in jindex inode.\n");
		return -1;
	}

	for(i = 0; i < sdp->md.journals; i++) {
		if((i % 2) == 0)
			log_at_notice(".");
		gfs2_recover_journal(sdp->md.journal[i], i);
		inode_put(sdp->md.journal[i],
			  (opts.no ? not_updated : updated));
	}
	log_notice("\nJournal recovery complete.\n");
	inode_put(sdp->master_dir, not_updated);
	inode_put(sdp->md.jiinode, not_updated);
	/* Sync the buffers to disk so we get a fresh start. */
	bsync(&sdp->buf_list);
	bsync(&sdp->nvbuf_list);
	return 0;
}
