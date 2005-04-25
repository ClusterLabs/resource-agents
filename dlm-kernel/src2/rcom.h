/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**  
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __RCOM_DOT_H__
#define __RCOM_DOT_H__

struct rcom_lock {
	uint32_t		rl_ownpid;
	uint32_t		rl_lkid;
	uint32_t		rl_remid;
	uint32_t		rl_parent_lkid;
	uint32_t		rl_parent_remid;
	uint32_t		rl_exflags;
	uint32_t		rl_flags;
	uint32_t		rl_lvbseq;
	int			rl_result;
	int8_t			rl_rqmode;
	int8_t			rl_grmode;
	int8_t			rl_status;
	int8_t			rl_asts;
	uint16_t		rl_wait_type;
	uint16_t		rl_namelen;
	uint64_t		rl_range[4];
	char			rl_lvb[DLM_LVB_LEN];
	char			rl_name[DLM_RESNAME_MAXLEN];
};

int dlm_rcom_status(struct dlm_ls *ls, int nodeid);
int dlm_rcom_names(struct dlm_ls *ls, int nodeid, char *last_name,int last_len);
int dlm_send_rcom_lookup(struct dlm_rsb *r, int dir_nodeid);
int dlm_send_rcom_lock(struct dlm_rsb *r, struct dlm_lkb *lkb);
void dlm_receive_rcom(struct dlm_header *hd, int nodeid);

#endif
