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

#ifndef __DIR_DOT_H__
#define __DIR_DOT_H__

int dlm_dir_lookup(gd_ls_t *ls, uint32_t nodeid, char *name, int namelen,
			uint32_t *r_nodeid, uint8_t *r_seq);
int dlm_dir_lookup_recovery(gd_ls_t *ls, uint32_t nodeid, char *name,
                            int namelen, uint32_t *r_nodeid);
uint32_t name_to_directory_nodeid(gd_ls_t *ls, char *name, int length);
uint32_t get_directory_nodeid(gd_res_t *rsb);
void remove_resdata(gd_ls_t *ls, uint32_t nodeid, char *name, int namelen,
		    uint8_t sequence);
int resdir_rebuild_local(gd_ls_t *ls);
int resdir_rebuild_send(gd_ls_t *ls, char *inbuf, int inlen, char *outbuf,
			int outlen, uint32_t nodeid);
int resdir_rebuild_wait(gd_ls_t * ls);
void resdir_clear(gd_ls_t *ls);
void resdir_dump(gd_ls_t *ls);
void process_expired_resdata(void);

#endif				/* __DIR_DOT_H__ */
