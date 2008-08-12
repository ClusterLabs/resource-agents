#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>

#include "gfs.h"
#include "dio.h"
#include "glock.h"
#include "glops.h"
#include "lm.h"
#include "lops.h"
#include "recovery.h"

#define bn2seg(bn) (((uint32_t)((bn) - jdesc->ji_addr)) / sdp->sd_sb.sb_seg_size)
#define seg2bn(seg) ((seg) * sdp->sd_sb.sb_seg_size + jdesc->ji_addr)

struct dirty_j {
	struct list_head dj_list;
	unsigned int dj_jid;
	struct gfs_jindex dj_desc;
};

/**
 * gfs_add_dirty_j - add a jid to the list of dirty journals
 * @sdp: the filesystem
 * @jid: the journal ID number
 *
 */

void
gfs_add_dirty_j(struct gfs_sbd *sdp, unsigned int jid)
{
	struct dirty_j *dj;

	dj = gmalloc(sizeof(struct dirty_j));
	memset(dj, 0, sizeof(struct dirty_j));

	dj->dj_jid = jid;

	spin_lock(&sdp->sd_dirty_j_lock);
	list_add(&dj->dj_list, &sdp->sd_dirty_j);
	spin_unlock(&sdp->sd_dirty_j_lock);
}

/**
 * get_dirty_j - return a dirty journal from the list
 * @sdp: the filesystem
 *
 * Returns: a struct dirty_j or NULL
 */

static struct dirty_j *
get_dirty_j(struct gfs_sbd *sdp)
{
	struct dirty_j *dj = NULL;

	spin_lock(&sdp->sd_dirty_j_lock);
	if (!list_empty(&sdp->sd_dirty_j)) {
		dj = list_entry(sdp->sd_dirty_j.prev, struct dirty_j, dj_list);
		list_del(&dj->dj_list);
	}
	spin_unlock(&sdp->sd_dirty_j_lock);

	return dj;
}

/**
 * gfs_clear_dirty_j - destroy the list of dirty journals
 * @sdp: the filesystem
 *
 */

void
gfs_clear_dirty_j(struct gfs_sbd *sdp)
{
	struct dirty_j *dj;
	for (;;) {
		dj = get_dirty_j(sdp);
		if (!dj)
			break;
		kfree(dj);
	}
}

/**
 * gfs_log_header - read the log header for a given segment
 * @sdp: the filesystem
 * @jdesc: the journal
 * @gl: the journal's glock
 * @seg: the segment to look at
 * @lh: the log header to return
 *
 * Read the log header for a given segement in a given journal.  Do a few
 * sanity checks on it.
 *
 * Returns: 0 on success, 1 if the header was invalid or incomplete and, errno on error
 */

static int
get_log_header(struct gfs_sbd *sdp, struct gfs_jindex *jdesc,
	       struct gfs_glock *gl, uint32_t seg, struct gfs_log_header *lh)
{
	struct buffer_head *bh;
	struct gfs_log_header lh2;
	int error;

	error = gfs_dread(gl, seg2bn(seg), DIO_START | DIO_WAIT, &bh);
	if (error)
		return error;

	gfs_log_header_in(lh, bh->b_data);
	gfs_log_header_in(&lh2,
			  bh->b_data + GFS_BASIC_BLOCK -
			  sizeof(struct gfs_log_header));

	brelse(bh);

	if (memcmp(lh, &lh2, sizeof(struct gfs_log_header)) != 0 ||
	    lh->lh_header.mh_magic != GFS_MAGIC ||
	    lh->lh_header.mh_type != GFS_METATYPE_LH)
		error = 1;

	return error;
}

/**
 * find_good_lh - find a good log header
 * @sdp: the filesystem
 * @jdesc: the journal
 * @gl: the journal's glock
 * @seg: the segment to start searching from (it's also filled in with a new value.) 
 * @lh: the log header to fill in
 * @forward: if true search forward in the log, else search backward
 *
 * Call get_log_header() to get a log header for a segment, but if the
 * segment is bad, either scan forward or backward until we find a good one.
 *
 * Returns: errno
 */

static int
find_good_lh(struct gfs_sbd *sdp, struct gfs_jindex *jdesc,
	     struct gfs_glock *gl, uint32_t *seg, struct gfs_log_header *lh,
	     int forward)
{
	int error;
	uint32_t orig_seg = *seg;

	for (;;) {
		error = get_log_header(sdp, jdesc, gl, *seg, lh);
		if (error <= 0)
			return error;

		if (forward) {
			if (++*seg == jdesc->ji_nsegment)
				*seg = 0;
		} else {
			if ((*seg)-- == 0)
				*seg = jdesc->ji_nsegment - 1;
		}

		if (*seg == orig_seg) {
			gfs_consist(sdp);
			return -EIO;
		}
	}
}

/**
 * verify_jhead - make sure we've found the head of the log
 * @sdp: the filesystem
 * @jdesc: the journal
 * @gl: the journal's glock
 * @head: this is filled in with the log descriptor of the head
 *
 * At this point, seg and lh should be either the head of the log or just
 * before.  Scan forward until we find the head.
 *
 * Returns: errno
 */

static int
verify_jhead(struct gfs_sbd *sdp, struct gfs_jindex *jdesc,
	     struct gfs_glock *gl, struct gfs_log_header *head)
{
	struct gfs_log_header lh;
	uint32_t seg;
	int error;

	seg = bn2seg(head->lh_first);

	for (;;) {
		if (++seg == jdesc->ji_nsegment)
			seg = 0;

		error = get_log_header(sdp, jdesc, gl, seg, &lh);
		if (error < 0)
			return error;

		if (error == 1)
			continue;
		if (lh.lh_sequence == head->lh_sequence)
			continue;

		if (lh.lh_sequence < head->lh_sequence)
			break;

		memcpy(head, &lh, sizeof(struct gfs_log_header));
	}

	return 0;
}

/**
 * gfs_find_jhead - find the head of a log
 * @sdp: the filesystem
 * @jdesc: the journal
 * @gl: the journal's glock
 * @head: the log descriptor for the head of the log is returned here
 *
 * Do a binary search of a journal and find the valid log entry with the
 * highest sequence number.  (i.e. the log head)
 *
 * Returns: errno
 */

int
gfs_find_jhead(struct gfs_sbd *sdp, struct gfs_jindex *jdesc,
	       struct gfs_glock *gl, struct gfs_log_header *head)
{
	struct gfs_log_header lh;
	uint32_t seg1, seg2, seg_m;
	int error;
	uint64_t lh1_sequence;

	seg1 = 0;
	seg2 = jdesc->ji_nsegment - 1;

	for (;;) {
		seg_m = (seg1 + seg2) / 2;

		error = find_good_lh(sdp, jdesc, gl, &seg1, &lh, TRUE);
		if (error)
			break;

		if (seg1 == seg_m) {
			error = verify_jhead(sdp, jdesc, gl, &lh);
			if (unlikely(error)) 
				printk("GFS: verify_jhead error=%d\n", error);
			else
				memcpy(head, &lh, sizeof(struct gfs_log_header));
			break;
		}

		lh1_sequence = lh.lh_sequence;

		error = find_good_lh(sdp, jdesc, gl, &seg_m, &lh, FALSE);
		if (error)
			break;

		if (lh1_sequence <= lh.lh_sequence)
			seg1 = seg_m;
		else
			seg2 = seg_m;
	}

	return error;
}

/**
 * gfs_increment_blkno - move to the next block in a journal
 * @sdp: the filesystem
 * @jdesc: the journal
 * @gl: the journal's glock
 * @addr: the block number to increment
 * @skip_header: if this is TRUE, skip log headers
 *
 * Replace @addr with the location of the next block in the log.
 * Take care of journal wrap and skip of log header if necessary.
 *
 * Returns: errno
 */

int
gfs_increment_blkno(struct gfs_sbd *sdp, struct gfs_jindex *jdesc,
		    struct gfs_glock *gl, uint64_t *addr, int skip_headers)
{
	struct gfs_log_header header;
	int error;

	(*addr)++;

	/* Handle journal wrap */

	if (*addr == seg2bn(jdesc->ji_nsegment))
		*addr -= jdesc->ji_nsegment * sdp->sd_sb.sb_seg_size;

	gfs_start_ra(gl, *addr,
		     jdesc->ji_addr +
		     jdesc->ji_nsegment * sdp->sd_sb.sb_seg_size - *addr);

	/* Handle landing on a header block */

	if (skip_headers && !do_mod(*addr, sdp->sd_sb.sb_seg_size)) {
		error = get_log_header(sdp, jdesc, gl, bn2seg(*addr), &header);
		if (error < 0)
			return error;

		if (error) { /* Corrupt headers here are bad */
			if (gfs_consist(sdp))
				printk("GFS: fsid=%s: *addr = %"PRIu64"\n",
				       sdp->sd_fsname, *addr);
			return -EIO;
		}
		if (header.lh_first == *addr) {
			if (gfs_consist(sdp))
				printk("GFS: fsid=%s: *addr = %"PRIu64"\n",
				       sdp->sd_fsname, *addr);
			gfs_log_header_print(&header);
			return -EIO;
		}

		(*addr)++;
		/* Can't wrap here */
	}

	return 0;
}

/**
 * foreach_descriptor - go through the active part of the log
 * @sdp: the filesystem
 * @jdesc: the journal
 * @gl: the journal's glock
 * @start: the first log header in the active region
 * @end: the last log header (don't process the contents of this entry))
 * @pass: the recovery pass
 *
 * Call a given function once for every log descriptor in the active
 * portion of the log.
 *
 * Returns: errno
 */

static int
foreach_descriptor(struct gfs_sbd *sdp, struct gfs_jindex *jdesc,
		   struct gfs_glock *gl, uint64_t start, uint64_t end,
		   unsigned int pass)
{
	struct gfs_log_header header;
	struct gfs_log_descriptor desc;
	struct buffer_head *bh;
	int error = 0;

	while (start != end) {
		if (do_mod(start, sdp->sd_sb.sb_seg_size)) {
			gfs_consist(sdp);
			return -EIO;
		}

		error = get_log_header(sdp, jdesc, gl, bn2seg(start), &header);
		if (error < 0)
			return error;

		if (error) { /* Corrupt headers here are bad */
			if (gfs_consist(sdp))
				printk("GFS: fsid=%s: start = %"PRIu64"\n",
				       sdp->sd_fsname, start);
			return -EIO;
		}
		if (header.lh_first != start) {
			if (gfs_consist(sdp))
				printk("GFS: fsid=%s: start = %"PRIu64"\n",
				       sdp->sd_fsname, start);
			gfs_log_header_print(&header);
			return -EIO;
		}

		start++;

		for (;;) {
			error = gfs_dread(gl, start, DIO_START | DIO_WAIT, &bh);
			if (error)
				return error;

			if (gfs_metatype_check(sdp, bh, GFS_METATYPE_LD)) {
				brelse(bh);
				return -EIO;
			}

			gfs_desc_in(&desc, bh->b_data);
			brelse(bh);

			if (desc.ld_type != GFS_LOG_DESC_LAST) {
				error = LO_SCAN_ELEMENTS(sdp, jdesc, gl, start,
							 &desc, pass);
				if (error)
					return error;

				while (desc.ld_length--) {
					error = gfs_increment_blkno(sdp, jdesc, gl,
								    &start, TRUE);
					if (error)
						return error;
				}
			} else {
				while (desc.ld_length--) {
					error = gfs_increment_blkno(sdp, jdesc, gl,
								    &start,
								    !!desc.ld_length);
					if (error)
						return error;
				}

				break;
			}
		}
	}

	return error;
}

/**
 * clean_journal - mark a dirty journal as being clean
 * @sdp: the filesystem
 * @jdesc: the journal
 * @gl: the journal's glock
 * @head: the head journal to start from
 *
 * Returns: errno
 */

static int noinline
clean_journal(struct gfs_sbd *sdp, struct gfs_jindex *jdesc,
	      struct gfs_glock *gl, struct gfs_log_header *head)
{
	struct gfs_log_header lh;
	struct gfs_log_descriptor desc;
	struct buffer_head *bh;
	uint32_t seg;
	uint64_t blkno;
	int error;

	seg = bn2seg(head->lh_first);

	for (;;) {
		if (++seg == jdesc->ji_nsegment)
			seg = 0;

		error = get_log_header(sdp, jdesc, gl, seg, &lh);
		if (error < 0)
			return error;

		/* Rewrite corrupt header blocks */

		if (error == 1) {
			bh = gfs_dgetblk(gl, seg2bn(seg));

			gfs_prep_new_buffer(bh);
			gfs_buffer_clear(bh);
			gfs_log_header_out(head, bh->b_data);
			gfs_log_header_out(head,
					   bh->b_data + GFS_BASIC_BLOCK -
					   sizeof(struct gfs_log_header));

			error = gfs_dwrite(sdp, bh, DIO_DIRTY | DIO_START | DIO_WAIT);
			brelse(bh);
			if (error)
				return error;
		}

		/* Stop when we get to the end of the log. */

		if (lh.lh_sequence < head->lh_sequence)
			break;
	}

	/*  Build a "last" descriptor for the transaction we are
	   about to commit by writing the shutdown header.  */

	memset(&desc, 0, sizeof(struct gfs_log_descriptor));
	desc.ld_header.mh_magic = GFS_MAGIC;
	desc.ld_header.mh_type = GFS_METATYPE_LD;
	desc.ld_header.mh_format = GFS_FORMAT_LD;
	desc.ld_type = GFS_LOG_DESC_LAST;
	desc.ld_length = 0;

	for (blkno = head->lh_first + 1; blkno != seg2bn(seg);) {
		if (do_mod(blkno, sdp->sd_sb.sb_seg_size))
			desc.ld_length++;
		if (++blkno == seg2bn(jdesc->ji_nsegment))
			blkno -= jdesc->ji_nsegment * sdp->sd_sb.sb_seg_size;
	}

	/*  Write the descriptor  */

	bh = gfs_dgetblk(gl, head->lh_first + 1);

	gfs_prep_new_buffer(bh);
	gfs_buffer_clear(bh);
	gfs_desc_out(&desc, bh->b_data);

	error = gfs_dwrite(sdp, bh, DIO_DIRTY | DIO_START | DIO_WAIT);
	brelse(bh);
	if (error)
		return error;

	/*  Build a log header that says the journal is clean  */

	memset(&lh, 0, sizeof(struct gfs_log_header));
	lh.lh_header.mh_magic = GFS_MAGIC;
	lh.lh_header.mh_type = GFS_METATYPE_LH;
	lh.lh_header.mh_format = GFS_FORMAT_LH;
	lh.lh_flags = GFS_LOG_HEAD_UNMOUNT;
	lh.lh_first = seg2bn(seg);
	lh.lh_sequence = head->lh_sequence + 1;
	/*  Don't care about tail  */
	lh.lh_last_dump = head->lh_last_dump;

	/*  Write the header  */

	bh = gfs_dgetblk(gl, lh.lh_first);

	gfs_prep_new_buffer(bh);
	gfs_buffer_clear(bh);
	gfs_log_header_out(&lh, bh->b_data);
	gfs_log_header_out(&lh,
			   bh->b_data + GFS_BASIC_BLOCK -
			   sizeof(struct gfs_log_header));

	error = gfs_dwrite(sdp, bh, DIO_DIRTY | DIO_START | DIO_WAIT);
	brelse(bh);

	return error;
}

/**
 * gfs_recover_journal - recover a given journal
 * @sdp: the filesystem
 * @jid: the number of the journal to recover
 * @jdesc: the struct gfs_jindex describing the journal
 * @wait: Don't return until the journal is clean (or an error is encountered)
 *
 * Acquire the journal's lock, check to see if the journal is clean, and
 * do recovery if necessary.
 *
 * Returns: errno
 */

int
gfs_recover_journal(struct gfs_sbd *sdp,
		    unsigned int jid, struct gfs_jindex *jdesc,
		    int wait)
{
	struct gfs_log_header *head;
	struct gfs_holder j_gh, t_gh;
	unsigned long t;
	int error;

	printk("GFS: fsid=%s: jid=%u: Trying to acquire journal lock...\n",
	       sdp->sd_fsname, jid);

	/*  Acquire the journal lock so we can do recovery  */

	error = gfs_glock_nq_num(sdp,
				 jdesc->ji_addr, &gfs_meta_glops,
				 LM_ST_EXCLUSIVE,
				 LM_FLAG_NOEXP |
				 ((wait) ? 0 : LM_FLAG_TRY) |
				 GL_NOCACHE, &j_gh);
	switch (error) {
	case 0:
		break;

	case GLR_TRYFAILED:
		printk("GFS: fsid=%s: jid=%u: Busy\n", sdp->sd_fsname, jid);
		error = 0;

	default:
		goto fail;
	};

	printk("GFS: fsid=%s: jid=%u: Looking at journal...\n",
	       sdp->sd_fsname, jid);

	head = kmalloc(sizeof(struct gfs_log_header), GFP_KERNEL);
	if (!head) {
		printk("GFS: fsid=%s jid=%u: Can't replay: Not enough memory",
		       sdp->sd_fsname, jid);
		goto fail_gunlock;
	}

	error = gfs_find_jhead(sdp, jdesc, j_gh.gh_gl, head);
	if (error)
		goto fail_header;

	if (!(head->lh_flags & GFS_LOG_HEAD_UNMOUNT)) {
		if (test_bit(SDF_ROFS, &sdp->sd_flags)) {
			printk("GFS: fsid=%s: jid=%u: Can't replay: read-only FS\n",
			       sdp->sd_fsname, jid);
			error = -EROFS;
			goto fail_header;
		}

		printk("GFS: fsid=%s: jid=%u: Acquiring the transaction lock...\n",
		       sdp->sd_fsname, jid);

		t = jiffies;

		/*  Acquire an exclusive hold on the transaction lock  */

		error = gfs_glock_nq_init(sdp->sd_trans_gl,
					  LM_ST_EXCLUSIVE,
					  LM_FLAG_NOEXP |
					  LM_FLAG_PRIORITY |
					  GL_NOCANCEL |
					  GL_NOCACHE,
					  &t_gh);
		if (error)
			goto fail_header;

		if (test_bit(SDF_ROFS, &sdp->sd_flags)) {
			printk("GFS: fsid=%s: jid=%u: Can't replay: read-only FS\n",
			       sdp->sd_fsname, jid);
			error = -EROFS;
			goto fail_gunlock_tr;
		}

		printk("GFS: fsid=%s: jid=%u: Replaying journal...\n",
		       sdp->sd_fsname, jid);

		set_bit(GLF_DIRTY, &j_gh.gh_gl->gl_flags);

		LO_BEFORE_SCAN(sdp, jid, head, GFS_RECPASS_A1);

		error = foreach_descriptor(sdp, jdesc, j_gh.gh_gl,
					   head->lh_tail, head->lh_first,
					   GFS_RECPASS_A1);
		if (error)
			goto fail_gunlock_tr;

		LO_AFTER_SCAN(sdp, jid, GFS_RECPASS_A1);

		gfs_replay_wait(sdp);

		error = clean_journal(sdp, jdesc, j_gh.gh_gl, head);
		if (error)
			goto fail_gunlock_tr;

		gfs_glock_dq_uninit(&t_gh);

		t = DIV_RU(jiffies - t, HZ);
		
		printk("GFS: fsid=%s: jid=%u: Journal replayed in %lus\n",
		       sdp->sd_fsname, jid, t);
	}

	gfs_lm_recovery_done(sdp, jid, LM_RD_SUCCESS);

	kfree(head);

	gfs_glock_dq_uninit(&j_gh);

	printk("GFS: fsid=%s: jid=%u: Done\n", sdp->sd_fsname, jid);

	return 0;

 fail_gunlock_tr:
	gfs_replay_wait(sdp);
	gfs_glock_dq_uninit(&t_gh);

 fail_header:
	kfree(head);

 fail_gunlock:
	gfs_glock_dq_uninit(&j_gh);

	printk("GFS: fsid=%s: jid=%u: %s\n",
	       sdp->sd_fsname, jid, (error) ? "Failed" : "Done");

 fail:
	gfs_lm_recovery_done(sdp, jid, LM_RD_GAVEUP);

	return error;
}

/**
 * gfs_check_journals - Recover any dirty journals
 * @sdp: the filesystem
 *
 */

void
gfs_check_journals(struct gfs_sbd *sdp)
{
	struct dirty_j *dj;

	for (;;) {
		dj = get_dirty_j(sdp);
		if (!dj)
			break;

		down(&sdp->sd_jindex_lock);

		if (dj->dj_jid != sdp->sd_lockstruct.ls_jid &&
		    dj->dj_jid < sdp->sd_journals) {
			memcpy(&dj->dj_desc,
			       sdp->sd_jindex + dj->dj_jid,
			       sizeof(struct gfs_jindex));
			up(&sdp->sd_jindex_lock);

			gfs_recover_journal(sdp,
					    dj->dj_jid, &dj->dj_desc,
					    FALSE);
			
		} else {
			up(&sdp->sd_jindex_lock);
			gfs_lm_recovery_done(sdp, dj->dj_jid, LM_RD_GAVEUP);
		}

		kfree(dj);
	}
}

/**
 * gfs_recover_dump - recover the log elements in this machine's journal
 * @sdp: the filesystem
 *
 * Returns: errno
 */

int
gfs_recover_dump(struct gfs_sbd *sdp)
{
	struct gfs_log_header head;
	int error;

	error = gfs_find_jhead(sdp, &sdp->sd_jdesc, sdp->sd_journal_gh.gh_gl,
			       &head);
	if (error)
		goto fail;

	if (!(head.lh_flags & GFS_LOG_HEAD_UNMOUNT)) {
		gfs_consist(sdp);
		return -EIO;
	}
	if (!head.lh_last_dump)
		return error;

	printk("GFS: fsid=%s: Scanning for log elements...\n",
	       sdp->sd_fsname);

	LO_BEFORE_SCAN(sdp, sdp->sd_lockstruct.ls_jid, &head, GFS_RECPASS_B1);

	error = foreach_descriptor(sdp, &sdp->sd_jdesc, sdp->sd_journal_gh.gh_gl,
				   head.lh_last_dump, head.lh_first,
				   GFS_RECPASS_B1);
	if (error)
		goto fail;

	LO_AFTER_SCAN(sdp, sdp->sd_lockstruct.ls_jid, GFS_RECPASS_B1);

	/* We need to make sure if we crash during the next log dump that
	   all intermediate headers in the transaction point to the last
	   log dump before the one we're making so we don't lose it. */

	sdp->sd_log_dump_last = head.lh_last_dump;

	printk("GFS: fsid=%s: Done\n", sdp->sd_fsname);

	return 0;

 fail:
	printk("GFS: fsid=%s: Failed\n", sdp->sd_fsname);

	return error;
}
