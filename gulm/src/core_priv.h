/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  2002-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/
#ifndef __gulm_core_priv_h__
#define __gulm_core_priv_h__
#include "xdr.h"
#include <arpa/inet.h>
/* proto types for all funcs that cross files in core need to go here.
 * If some non core_* file needs a prototype, it needs to get it from a
 * different file!
 */
/* core_fence */
void init_fence(void);
void queue_node_for_fencing(uint8_t *Name);
int check_for_zombied_stomiths(pid_t pid, int status);

/* core_resources */
int init_resources(void);
int send_mbrshp_to_children(char *name, int st);
int send_core_state_to_children(void);
void dump_resources(void);
void serialize_resources(xdr_enc_t *enc);
int add_resource(char *name, int poll_idx, uint32_t options);
int release_resource(char *name);
int die_with_me(char *name);

/* core_nodelist */
int init_nodes(void);
void release_nodelist(void);
int send_mbrshp_to_slaves(char *name, int st);
int send_quorum_to_slaves(void);
void dump_nodes(void);
int add_node(char *name, struct in6_addr *ip);
int lookup_nodes_ip(char *name, struct in6_addr *ip);
int set_nodes_mode(char *name, int mode);
int Mark_Loggedin(char *name);
int Mark_Loggedout(char *name);
int Mark_lgout_from_Exp(char *name);
int Mark_Expired(char *name);
void Die_if_expired(char *name);
int Force_Node_Expire(char *name);
int beat_node(char *name, int poll_idx);
int check_beats(void);
int beat_all_once(void);
int fence_all_expired(void);
int Mark_Old_Master_lgin(void);
int tag_for_lost(void);
int Logout_leftovers(void);
int get_node(xdr_enc_t *enc, char *name);
int serialize_node_list(xdr_enc_t *enc);
int deserialize_node_list(xdr_dec_t *dec);

/* core_io */
int open_max(void);
char *get_Master_Name(void);
int init_core_poller(void);
int open_core_listener(int port);
void release_core_poller(void);
int do_logout(void);
void decrement_quorumcount(void);
int send_update(int poll_idx, char *name, int st, struct in6_addr *ip);
int send_quorum(int poll_idx);
int send_core_state_update(int poll_idx);
void switch_into_Pending(void);
void work_loop(void);

void close_by_idx(int idx);
int add_resource_poller(int fd, char *name);

#endif /*__gulm_core_priv_h__*/

/* vim: set ai cin et sw=3 ts=3 : */
