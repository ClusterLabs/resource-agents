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

#ifndef __MIDCOMMS_DOT_H__
#define __MIDCOMMS_DOT_H__

int midcomms_send_message(uint32_t csid, struct gd_req_header *msg,
			  int allocation);
int midcomms_process_incoming_buffer(int csid, const void *buf, unsigned offset,
				     unsigned len, unsigned limit);
void midcomms_send_buffer(struct gd_req_header *msg,
			  struct writequeue_entry *e);

#endif				/* __MIDCOMMS_DOT_H__ */
