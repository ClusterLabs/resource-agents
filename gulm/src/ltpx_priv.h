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
#ifndef __ltpx_priv_h__
#define __ltpx_priv_h__
#include "hash.h"
#include "Qu.h"

typedef struct lock_req_s {
   LLi_t ls_list;

   uint32_t code; /* gulm_lock_state_req or gulm_lock_action_req */
   uint8_t  *key;
   uint16_t keylen;
   uint8_t  state; /* or action if this is an action req */
   uint32_t flags;
   uint8_t  *lvb; /* drop overloads this to be name as well */
   uint16_t lvblen;

   int poll_idx;/* who made this request. */

} lock_req_t;

int init_ltpx_poller(void);
int open_ltpx_listener(int port);
int open_ltpx_to_core(void);
void ltpx_main_loop(void);


/* these will move/change here once i figure out exactly how i want to
 * orginise the code.
 */
int initialize_ltpx_maps(void);
lock_req_t *get_new_lock_req(void);
void recycle_lock_req(lock_req_t *lq);
hash_t * create_new_req_map(void);
void dump_ltpx_locks(hash_t *map, int ltid);
void dump_all_master_tables(void);
void dump_ltpx_senders(Qu_t *head, int ltid);

#endif /*__ltpx_priv_h__*/
