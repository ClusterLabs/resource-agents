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

void print_name(char *b, int len);
uint32_t name_to_directory_nodeid(struct dlm_ls *ls, char *name, int length);
uint32_t get_directory_nodeid(struct dlm_rsb *rsb);

int dlm_dir_lookup(struct dlm_ls *ls, uint32_t nodeid, char *name, int namelen,
			uint32_t *r_nodeid);
void dlm_dir_remove(struct dlm_ls *ls, uint32_t nodeid, char *name,
		    int namelen);
int dlm_dir_rebuild_local(struct dlm_ls *ls);
int dlm_dir_rebuild_send(struct dlm_ls *ls, char *inbuf, int inlen,
			 char *outbuf, int outlen, uint32_t nodeid);
int dlm_dir_rebuild_wait(struct dlm_ls * ls);
void dlm_dir_clear(struct dlm_ls *ls);
void dlm_dir_dump(struct dlm_ls *ls);
void clear_free_de(struct dlm_ls *ls);

#endif				/* __DIR_DOT_H__ */
