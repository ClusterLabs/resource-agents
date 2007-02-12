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
		write_journal(sdp, sdp->md.journal[i], i,
					  sdp->md.journal[i]->i_di.di_size / sdp->sd_sb.sb_bsize);
		/* Can't use d_di.di_blocks because that also includes metadata. */
	}
	log_notice("\nJournals cleared.\n");
	return 0;
}
