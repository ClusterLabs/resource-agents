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

#ifndef __SM_MESSAGE_DOT_H__
#define __SM_MESSAGE_DOT_H__

void init_messages(void);
uint32_t sm_new_global_id(int level);
void smsg_bswap_out(sm_msg_t * smsg);
char *create_smsg(sm_group_t *sg, int type, int datalen, int *msglen,
		  sm_sevent_t *sev);
void process_messages(void);
int sm_cluster_message(char *msg, int len, char *addr, int addr_len,
		       unsigned int node_id);
int send_nodeid_message(char *msg, int len, uint32_t nodeid);
int send_broadcast_message(char *msg, int len);
int send_broadcast_message_sev(char *msg, int len, sm_sevent_t * sev);
int send_members_message(sm_group_t *sg, char *msg, int len);
int send_members_message_sev(sm_group_t *sg, char *msg, int len,
			     sm_sevent_t * sev);
int sm_cluster_message(char *msg, int len, char *addr, int addr_len,
		       unsigned int node_id);

#endif
