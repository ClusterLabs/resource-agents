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

#ifndef __LOWCOMMS_DOT_H__
#define __LOWCOMMS_DOT_H__

/* The old interface */
int lowcomms_send_message(int csid, char *buf, int len, int allocation);

/* The new interface */
struct writequeue_entry;
extern struct writequeue_entry *lowcomms_get_buffer(int nodeid, int len,
						    int allocation, char **ppc);
extern void lowcomms_commit_buffer(struct writequeue_entry *e);

int lowcomms_start(void);
void lowcomms_stop(void);
void lowcomms_stop_accept(void);
int lowcomms_close(int nodeid);
int lowcomms_max_buffer_size(void);

int lowcomms_our_nodeid(void);

#endif				/* __LOWCOMMS_DOT_H__ */
