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

#ifndef __RECCOMMS_DOT_H__
#define __RECCOMMS_DOT_H__

/* Bit flags */

#define RESDIR_VALID            (1)
#define RESDIR_ALL_VALID        (2)
#define NODES_VALID             (4)
#define NODES_ALL_VALID         (8)

#define RECCOMM_STATUS          (1)
#define RECCOMM_RECOVERNAMES    (2)
#define RECCOMM_GETMASTER       (3)
#define RECCOMM_BULKLOOKUP      (4)
#define RECCOMM_NEWLOCKS        (5)
#define RECCOMM_NEWLOCKIDS      (6)
#define RECCOMM_REMRESDATA      (7)

int rcom_send_message(struct dlm_ls *ls, uint32_t nodeid, int type,
		      struct dlm_rcom *rc, int need_reply);
void process_recovery_comm(uint32_t nodeid, struct dlm_header *header);

#endif
