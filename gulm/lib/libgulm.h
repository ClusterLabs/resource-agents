/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
**
*******************************************************************************
******************************************************************************/
#ifndef __libgulm_h__
#define __libgulm_h__

/* bit messy, but we need this to be rather seemless in both kernel and
 * userspace. and this seems the easiest way to do it.
 */
#ifdef __KERNEL__

#ifdef __linux__
typedef struct socket* lg_socket;
#endif /*__linux__*/
#else /*__KERNEL__*/
typedef int lg_socket;
#endif /*__KERNEL__*/

typedef void * gulm_interface_p;

/* mallocs the interface structure.
 */
int lg_initialize(gulm_interface_p *, char *cluster_name, char *service_name);
/* frees struct.
 */
void lg_release(gulm_interface_p);

/* if you need to use ports other than the defaults. */
int lg_set_core_port(gulm_interface_p lgp, uint16_t new);
int lg_set_lock_port(gulm_interface_p lgp, uint16_t new);

/* Determins where we are with a itemlist callback */
typedef enum {lglcb_start, lglcb_item, lglcb_stop} lglcb_t;

/****** Core specifics ******/

/* leaving a callback pointer as NULL, will cause that message type to 
 * be ignored. */
typedef struct lg_core_callbacks_s { 
  int (*login_reply)(void *misc, uint64_t gen, uint32_t error, uint32_t rank, 
                     uint8_t corestate); 
  int (*logout_reply)(void *misc); 
  int (*nodelist)(void *misc, lglcb_t type, char *name,
                  struct in6_addr *ip, uint8_t state); 
  int (*statechange)(void *misc, uint8_t corestate,
                     struct in6_addr *masterip, char *mastername); 
  int (*nodechange)(void *misc, char *nodename,
                    struct in6_addr *nodeip, uint8_t nodestate); 
  int (*service_list)(void *misc, lglcb_t type, char *service); 
  int (*error)(void *misc, uint32_t err);
} lg_core_callbacks_t;

/* this will trigger a callback from gulm_core_callbacks_t 
 * handles one message! Either stick this inside of a thread,
 * or in a poll()/select() loop using the function below.
 * This will block until there is a message sent from core. 
 */
int lg_core_handle_messages(gulm_interface_p, lg_core_callbacks_t*, void *misc);

/* this returns the filedescriptor that the library is using to 
 * communicate with the core. This is only for using in a poll() 
 * or select() call to avoid having the gulm_core_handle_messages()
 * call block. 
 */
lg_socket lg_core_selector(gulm_interface_p);

/* Queue requests. */
int lg_core_login(gulm_interface_p, int important);
int lg_core_logout(gulm_interface_p);
int lg_core_nodeinfo(gulm_interface_p, char *nodename);
int lg_core_nodelist(gulm_interface_p);
int lg_core_servicelist(gulm_interface_p);
int lg_core_corestate(gulm_interface_p);

/* for completeness mostly. */
int lg_core_shutdown(gulm_interface_p);
int lg_core_forceexpire(gulm_interface_p, char *node_name);
int lg_core_forcepending(gulm_interface_p);

/* Node states
 * First three are actual states, as well as changes.  Last is only a node
 * change message.
 * */
#define lg_core_Logged_in  (0x05)
#define lg_core_Logged_out (0x06)
#define lg_core_Expired    (0x07)
#define lg_core_Fenced     (0x08)
/* Core states */
#define lg_core_Slave       (0x01)
#define lg_core_Master      (0x02)
#define lg_core_Pending     (0x03)
#define lg_core_Arbitrating (0x04)
#define lg_core_Client      (0x06)

/****** lock space specifics *****/
/* note that this library masks out the lock table seperation. 
 */

typedef struct lg_lockspace_callbacks_s { 
  int (*login_reply)(void *misc, uint32_t error, uint8_t which); 
  int (*logout_reply)(void *misc); 
  int (*lock_state)(void *misc, uint8_t *key, uint16_t keylen,
                    uint64_t subid, uint64_t start, uint64_t stop,
                    uint8_t state, uint32_t flags, uint32_t error,  
                    uint8_t *LVB, uint16_t LVBlen); 
  int (*lock_action)(void *misc, uint8_t *key, uint16_t keylen,
                     uint64_t subid, uint8_t action, uint32_t error); 
  int (*drop_lock_req)(void *misc, uint8_t *key, uint16_t keylen,
                       uint64_t subid, uint8_t state);
  int (*drop_all)(void *misc); 
  int (*error)(void *misc, uint32_t err);
} lg_lockspace_callbacks_t;

/* Like the core handle messages function, but for the lockspace.
 * Handles one message, blocks.
 */

int lg_lock_handle_messages(gulm_interface_p, lg_lockspace_callbacks_t*,
                            void *misc);

/* this returns the filedescriptor that the library is using to 
 * communicate with the ltpx. This is only for using in a poll() 
 * or select() call to avoid having the gulm_lock_handle_messages()
 * call block. 
 */
lg_socket lg_lock_selector(gulm_interface_p);

/* Lockspace request calls */
int lg_lock_login(gulm_interface_p, uint8_t lockspace[4] );
int lg_lock_logout(gulm_interface_p);
int lg_lock_state_req(gulm_interface_p, uint8_t *key, uint16_t keylen, 
                      uint64_t subid, uint64_t start, uint64_t stop,
                      uint8_t state, uint32_t flags,
                      uint8_t *LVB,  uint16_t LVBlen);
/* cancel always works on your last pending request.  You can only have one
 * request pending per key,host,subid triplet. So you don't need to specify
 * ranges for a cancel. (which would be confusing.)
 */
int lg_lock_cancel_req(gulm_interface_p, uint8_t *key, uint16_t keylen,
                       uint64_t subid);

/* actions opertate on a lock, not sub ranges. */
int lg_lock_action_req(gulm_interface_p, uint8_t *key,  
                       uint16_t keylen, uint64_t subid, uint8_t action, 
                       uint8_t *LVB, uint16_t LVBlen);
int lg_lock_drop_exp(gulm_interface_p, uint8_t *holder,
                     uint8_t *keymask, uint16_t kmlen);

/* state requests */
#define lg_lock_state_Unlock    (0x00)
#define lg_lock_state_Exclusive (0x01)
#define lg_lock_state_Deferred  (0x02)
#define lg_lock_state_Shared    (0x03)

/* actions */
#define lg_lock_act_HoldLVB     (0x0b)
#define lg_lock_act_UnHoldLVB   (0x0c)
#define lg_lock_act_SyncLVB     (0x0d)

/* flags */
#define lg_lock_flag_DoCB        (0x00000001)
#define lg_lock_flag_Try         (0x00000002)
#define lg_lock_flag_Any         (0x00000004)
#define lg_lock_flag_IgnoreExp   (0x00000008)
#define lg_lock_flag_Cachable    (0x00000020)
#define lg_lock_flag_Piority     (0x00000040)
#define lg_lock_flag_NoCallBacks (0x00000100)


/* These are the possible values that can be in the error fields. */
#define lg_err_Ok              (0)
#define lg_err_BadLogin        (1001)
#define lg_err_BadCluster      (1003)
#define lg_err_BadConfig       (1004)
#define lg_err_BadGeneration   (1005)
#define lg_err_BadWireProto    (1019)

#define lg_err_NotAllowed      (1006)
#define lg_err_Unknown_Cs      (1007)
#define lg_err_BadStateChg     (1008)
#define lg_err_MemoryIssues    (1009)

#define lg_err_TryFailed       (1011)
#define lg_err_AlreadyPend     (1013)
#define lg_err_Canceled        (1015)

#define lg_err_NoSuchFS        (1016)
#define lg_err_NoSuchJID       (1017)
#define lg_err_NoSuchName      (1018)


#endif /*__libgulm_h__*/
/* vim: set ai cin et sw=3 ts=3 : */
