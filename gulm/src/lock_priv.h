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
#ifndef __gulm_lock_priv_h__
#define __gulm_lock_priv_h__
#include "LLi.h"
#include "Qu.h"
#include "xdr.h"
/* these get used in both io and space, so we'll drop them here. */
typedef struct waiters_s {
   Qu_t      wt_list;
   uint8_t  *name;
   uint64_t  subid;
   uint8_t  *key;
   uint16_t  keylen;
   uint32_t  op;
   uint8_t   state;
   uint32_t  flags;
   uint64_t  start;
   uint64_t  stop;
   uint8_t  *LVB;
   uint16_t  LVBlen;
   /* stuff for replies.*/
   uint8_t Slave_rpls; /* bitmask of which slaves have replied */
   uint8_t Slave_sent; /* which slaves we sent the update to */
   int idx; /* where to send replies. (index into the pollers) */
   int ret; /* what was the result code of this request? */

   /* track a couple of extra things when we're keeping history. */
#ifdef LOCKHISTORY
   uint64_t starttime; /* when did we get this request? */
   uint64_t stoptime;  /* when did we make it history? */
#endif
}Waiters_t;
/* uses 168 bytes on 32bits
 *      200 bytes on 64bits
 */

typedef struct Holders_s {
	LLi_t    cl_list;
   uint8_t *name;
   uint64_t subid;
   uint8_t  state;
   uint64_t start; /* range start */
   uint64_t stop;  /* reange stop */
   int      idx; /* used by the send_drp_req() function.  It is a caching of
                 * the idx offset into the pollers.  It is checked to be
                 * valid before use, and if wrong updated.  As such, it
                 * should be inited to 0 and ignored by others.
                 */
} Holders_t;
/* uses 37 on 32bits
 *      57 on 64bits
 */

typedef struct Lock_s {
   LLi_t     lk_list;
   uint8_t   *key;
   uint8_t   keylen;
   uint8_t   LVBlen;
   uint8_t   *LVB;

   uint32_t  HolderCount;
   LLi_t     Holders;
   uint32_t  LVB_holder_cnt;
   LLi_t     LVB_holders; /* have rights to LVB, mayormaynot have lock state*/

   uint32_t  ExpiredCount;
   LLi_t     ExpHolders;

   Qu_t      Waiters; /* how big this list is depends on lock state */
   Qu_t      High_Waiters; /* these have a higher priority than normal reqs.*/

   Qu_t      Action_Waiters; /* Actions sit here until reply_waiter is open */
   Qu_t      State_Waiters;

   Waiters_t *reply_waiter; /*where lkrq sit until they get all slave replies.*/


#ifdef LOCKHISTORY
   uint32_t  Histlen;
   Qu_t      History;  /* If active, we keep the last couple of request
                        * structs here.  for debugging stuff.
                        * sucks memory like you cannot believe.
                        */
#endif

} Lock_t;
/* uses 271 on 32bits
 *      391 on 64bits
 */

/* About the queues in Lock_t
 * Yeah, there are a bunch of them.  In basic form, there are three queues.
 * These are then broken into sub parts, to provide specific features
 * within each of the queues.
 *
 * At the top level:
 *  The reply_waiter queue. Cleverly disguised as a single pointer.
 *    This is where a request sits until all of the slave nodes have
 *    acked that request.
 *  The Incomming Queue.
 *    Action_waiters and State_Waiters.
 *    New requests are put here.  No processing of any kind has been done
 *    yet. (save for a few special cases.)
 *  The Conflict Queue.
 *    The Waiters and High_Waiters.
 *    If the lock request is incompatible with the current state of the
 *    lock, and must wait for a change before it can be completed, it is
 *    placed onto this queue.
 *    
 *
 *
 */

/* from io */
int init_lt_poller(void);
int open_lt_listener(int port);
int open_lt_to_core(void);
int send_req_lk_reply(Waiters_t *lkrq, Lock_t *lk, uint32_t retcode);
int send_act_lk_reply(Waiters_t *lkrq, uint32_t retcode);
void send_req_update_to_slaves(Waiters_t *lkrq);
void send_act_update_to_slaves(Waiters_t *lkrq);
void send_update_reply_to_master(Waiters_t *lkrq);
void send_drp_req(Lock_t *lk, Waiters_t *lkrq);
void send_drop_all_req(void);
void lt_main_loop(void);

/* from space */
int init_lockspace(unsigned long maxlocks, unsigned int hashbuckets);
void dump_locks(void);
int send_stats(xdr_enc_t *enc);
void check_fullness(void);
Waiters_t *get_new_lkrq(void);
void recycle_lkrq(Waiters_t *lkrq);
Waiters_t *duplicate_lkrw(Waiters_t *old);
#ifdef LOCKHISTORY
void record_lkrq(Lock_t *lk, Waiters_t *lkrq);
#endif
void delete_entire_waiters_list( Qu_t *q);
int force_lock_state(Waiters_t *lkrq);
int force_lock_action(Waiters_t *lkrq);
int update_lock_state(Waiters_t *lkrq);
int do_lock_state(Waiters_t *lkrq);
int do_lock_action(Waiters_t *lkrq);
int increment_slave_update_replies(uint8_t *key, uint16_t len,
      int slave, uint8_t smask);
void recheck_reply_waiters(uint8_t Slave_bits, uint8_t onlogin);
void __inline__ expire_locks(uint8_t *name);
void __inline__ drop_expired(uint8_t *name, uint8_t *, uint16_t);
void __inline__ rerun_wait_queues(void);
int serialize_lockspace(int fd);
int deserialize_lockspace(int fd);
int list_expired_holders(xdr_enc_t *enc);
int __inline__ compare_holder_waiter_names(Holders_t *h, Waiters_t *w);

#endif /*__gulm_lock_priv_h__*/
/* vim: set ai cin et sw=3 ts=3 : */
