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
#ifndef __gio_wiretypes_h__
#define __gio_wiretypes_h__

/* an attempt to do something about tracking changes to the protocol over
 * the wires.
 * If I was really cute, this would be effectivily a checksum of this file.
 */
#define GIO_WIREPROT_VERS (0x67000014)

/*****************Error codes.
 * everyone uses these same error codes.
 */
#define gio_Err_Ok              (0)
#define gio_Err_BadLogin        (1001)
#define gio_Err_BadCluster      (1003)
#define gio_Err_BadConfig       (1004)
#define gio_Err_BadGeneration   (1005)
#define gio_Err_BadWireProto    (1019)

#define gio_Err_NotAllowed      (1006)
#define gio_Err_Unknown_Cs      (1007)
#define gio_Err_BadStateChg     (1008)
#define gio_Err_MemoryIssues    (1009)

#define gio_Err_TryFailed       (1011)
#define gio_Err_AlreadyPend     (1013)
#define gio_Err_Canceled        (1015)

/* next free error code: 1002 1010 1012 1014 1016 1017 1018 1020 */

/*
 * Error:  just sort of a generic error code thing.
 *    uint32: gERR
 *    uint32: opcode that this is in reply to. (can be zeros)
 *    uint32: error code
 */
#define gulm_err_reply (0x67455252) /* gERR */

#define gulm_nop (0x674e4f50)  /* gNOP */

/********************* Core *****************/
/* 
 * login request
 *    uint32: gCL0
 *    uint32: proto version
 *    string: cluster ID
 *    string: My Name
 *    uint64: generation number
 *    uint32: config CRC
 *    uint32: rank
 * login reply
 *    uint32: gCL1
 *    uint64: generation number
 *    uint32: error code
 *    uint32: rank
 *    uint8:  ama
 *   If I am the Master or Arbitrating and there are no errors, A
 *   serialization of the current nodelist follows. And a client or slave
 *   is connecting (not resources).
 *
 * logout request:
 *    uint32: gCL2
 *    string: node name
 *    uint8:  S/P/A/M/R
 * logout reply:   Don't seem to use this....
 *    uint32: gCL3
 *    uint32: error code
 *
 * resource login request:
 *    uint32: gCL4
 *    uint32: proto version
 *    string: cluster ID
 *    string: resource name
 *    uint32: options
 *  login reply (gCL1) is sent in return.
 *
 * beat req
 *    uint32: gCB0
 *    string: My Name
 * beat rpl
 *    uint32: gCB1
 *    uint32: error code
 *
 * Membership Request
 *    uint32: gCMA
 *    string: node name
 *
 * Membership update
 *    uint32: gCMU
 *    string: node name
 *    IPv6:   IP
 *    uint8:  Current State
 *
 * Membership list request info.
 *    uint32: gCMl
 *
 * Membership list info.
 *    uint32: gCML
 *    list_start_marker
 *     string: node name
 *     IPv6:   IP
 *     uint8:  state
 *     uint8:  laststate
 *     uint8:  mode (S/P/A/M/C)
 *     uint32: missed beats
 *     uint64: last beat
 *     uint64: delay avg
 *     uint64: max delay
 *    list_stop_marker
 *
 * Request Resource info
 *    uint32: gCR0
 *
 * Resource list info
 *    uint32: gCR1
 *    list_start_marker
 *     string: name
 *    list_stop_marker
 *
 * Force node into Expired:
 *    uint32: gCFE
 *    string: node name
 *
 * Core state request:
 *    uint32: gCSR
 *
 * Core state changes:
 *    uint32: gCSC
 *    uint8:  state  (slave, pending, arbitrating, master)
 *    uint8:  quorate (true/false)
 *  If state == Slave, then the next two will follow.
 *    IPv6:   MasterIP
 *    string: MasterName
 *
 * Quorum Change:
 *    uint32: gCQC
 *    uint8:  quorate (true/false)
 *
 * Core shutdown req:
 *    uint32: gCSD
 *
 * Switch core from current state into Pending:
 *    uint32: gCSP
 *
 */
#define gulm_core_login_req  (0x67434c00) /* gCL0 */
#define gulm_core_login_rpl  (0x67434c01) /* gCL1 */
#define gulm_core_logout_req (0x67434c02) /* gCL2 */
#define gulm_core_logout_rpl (0x67434c03) /* gCL3 */
#define gulm_core_reslgn_req (0x67434c04) /* gCL4 */
#define gulm_core_beat_req   (0x67434200) /* gCB0 */
#define gulm_core_beat_rpl   (0x67434201) /* gCB1 */
#define gulm_core_mbr_req    (0x67434d41) /* gCMA */
#define gulm_core_mbr_updt   (0x67434d55) /* gCMU */
#define gulm_core_mbr_lstreq (0x67434d6c) /* gCMl */
#define gulm_core_mbr_lstrpl (0x67434d4c) /* gCML */
#define gulm_core_mbr_force  (0x67434645) /* gCFE */
#define gulm_core_res_req    (0x67435200) /* gCR0 */
#define gulm_core_res_list   (0x67435201) /* gCR1 */
#define gulm_core_state_req  (0x67435352) /* gCSR */
#define gulm_core_state_chgs (0x67435343) /* gCSC */
#define gulm_core_quorm_chgs (0x67435143) /* gCSC */
#define gulm_core_shutdown   (0x67435344) /* gCSD */
#define gulm_core_forcepend  (0x67435350) /* gCSP */

/* in the st field */
#define gio_Mbr_Logged_in  (0x05)
#define gio_Mbr_Logged_out (0x06)
#define gio_Mbr_Expired    (0x07)
#define gio_Mbr_Killed     (0x08)
#define gio_Mbr_OM_lgin    (0x09)

/* in the ama field */
#define gio_Mbr_ama_Slave       (0x01)
#define gio_Mbr_ama_Master      (0x02)
#define gio_Mbr_ama_Pending     (0x03)
#define gio_Mbr_ama_Arbitrating (0x04)
#define gio_Mbr_ama_Resource    (0x05)
#define gio_Mbr_ama_Client      (0x06)
/* the Client entery is ONLY for mode tracking.
 * nodelist reply is the only place it is used.
 */

/* options that affect behavors on services. (resources) */
#define gulm_svc_opt_important (0x00000001)
#define gulm_svc_opt_locked    (0x00000002)

/********************* Info Traffic *****************
 *
 * Note that for many of these, they can be sent to all of the servers and
 * will get sane replies.  Some of these can only be sent to specific
 * servers.
 *
 * stats req:
 *    uint32: gIS0
 * stats rpl:
 *    uint32: gIS1
 *    list start:
 *       string: key
 *       string: value
 *    list stop:
 * Notes:
 *  The stats reply is a set of string pairs.  This way the server can send
 *  whatever things it wants, and the same client code will work for
 *  anything.
 *
 * set verbosity:
 *    uint32: gIV0
 *    string: verb flags (with -/+) to [un]set
 * Note:
 *  We don't bother with a reply for this.  If the server got it, it works.
 *  If it didn't, it cannot send an error back anyways.
 *
 * close socket:
 *   uint32: gSC0
 * Note:
 *   Tells the server to close this connection cleanly.  We're done with
 *   it.  This is *not* the same as loging out.  You must login before you
 *   can logout.  And many commands sent from gulm_tool happen without
 *   logging in.  These commands would be useful for clients in many cases,
 *   so I don't want to put a close at the end of them, but if I don't,
 *   there will be error messages printed on the console when gulm_tool
 *   calls them.
 *   So we need a way to close a connection cleanly that has not been
 *   logged in.
 *
 * request slave list:
 *    uint32: gIL0
 * slave list replay:
 *    uint32: gIL1
 *    list start:
 *       string: name
 *       uint32: poller idx
 *    list stop:
 */
#define gulm_info_stats_req      (0x67495300) /* gIS0 */
#define gulm_info_stats_rpl      (0x67495301) /* gIS1 */
#define gulm_info_set_verbosity  (0x67495600) /* gIV0 */
#define gulm_socket_close        (0x67534300) /* gSC0 */
#define gulm_info_slave_list_req (0x67494c00) /* gIL0 */
#define gulm_info_slave_list_rpl (0x67494c01) /* gIL1 */

/********************* Lock Traffic *****************
 * All lock traffic.
 *
 * login req:
 *    uint32: gLL0
 *    uint32: proto version
 *    string: node name
 *    uint8:  Client/Slave
 * login rpl:
 *    uint32: gLL1
 *    uint32: error code
 *    uint8:  Slave/Master
 *    xdr of current lock state if no errors and master sending reply
 *       and you're a slave.
 *       uh, i think i assume that it is only four bytes in some places.
 *       Need to look into this...
 *
 * logout req:
 *    uint32: gLL2
 * logout rpl:
 *    uint32: gLL3
 *
 * select lockspace:
 *    uint32: gLS0
 *    raw:    usually just four bytes for lockspace name.
 *            but can be most anything.
 *
 * lock req:
 *    uint32: gLR0
 *    raw:    key
 *    uint64: sub id
 *    uint64: start
 *    uint64: stop
 *    uint8:  state
 *    uint32: flags
 *    raw:    lvb -- Only exists if hasLVB flag is true.
 * lock rpl:
 *    uint32: gLR1
 *    raw:    key
 *    uint64: sub id
 *    uint64: start
 *    uint64: stop
 *    uint8:  state
 *    uint32: flags
 *    uint32: error code
 *    raw:    lvb -- Only exists if hasLVB flag is true.
 *
 * lock state update:
 *    uint32: gLRU
 *    string: node name
 *    uint64: sub id
 *    uint64: start
 *    uint64: stop
 *    raw:    key
 *    uint8:  state
 *    uint32: flags
 *    raw:    lvb -- Only exists if hasLVB flag is true.
 *
 * Action req:
 *    uint32: gLA0
 *    raw:    key
 *    uint64: sub id
 *    uint8:  action
 *    raw:    lvb -- Only exists if action is SyncLVB
 * Action Rpl:
 *    uint32: gLA1
 *    raw:    key
 *    uint64: sub id
 *    uint8:  action
 *    uint32: error code
 *
 * Action update:
 *    uint32: gLAU
 *    string: node name
 *    uint64: sub id
 *    raw:    key
 *    uint8:  action
 *    raw:    lvb -- Only exists if action is SyncLVB
 *
 * Slave Update Rply:   -- for both actions and requests.
 *    uint32: gLUR
 *    raw:    key
 *
 * Query Lock request:
 *    uint32: gLQ0
 *    raw:    key
 *    uint64: subid
 *    uint64: start
 *    uint64: stop
 *    uint8:  state
 * 
 * Query Lock Reply:
 *    uint32: gLQ1
 *    raw:    key
 *    uint64: subid
 *    uint64: start
 *    uint64: stop
 *    uint8:  state
 *    uint32: error
 *    list start mark
 *     string: node
 *     uint64: subid
 *     uint64: start
 *     uint64: stop
 *     uint8:  state
 *    list stop mark
 *
 * Drop lock Callback:
 *    uint32: gLC0
 *    raw:    key
 *    uint64: subid
 *    uint8:  state
 *
 * Drop all locks callback:  This is the highwater locks thing
 *    uint32: gLC2
 *
 * Drop expired locks:
 *    uint32: gLEO
 *    string: node name  if NULL, then drop all exp for mask.
 *    raw:    keymask  if keymask & key == key, then dropexp on this lock.
 *
 * Expire Locks:
 *    uint32: gLEE
 *    string: node name  cannot be NULL
 *    raw:    keymask  if keymask & key == key, then expire on this lock.
 *
 * Lock list req:
 *    uint32: gLD0
 * Lock list rpl:
 *    uint32: gLD1
 *    list start mark
 *     uint8: key length
 *     raw:   key
 *     uint8: lvb length
 *     if lvb length > 0, raw: LVB
 *     uint32: Holder count
 *     list start mark
 *      string: holders
 *      uint64: subid
 *      uint8: state
 *      uint64: start
 *      uint64: stop
 *     list stop mark
 *     uint32: LVB holder count
 *     list start mark
 *      string: LVB Holders
 *      uint64: subid
 *     list stop mark
 *     uint32: Expired holder count
 *     list start mark
 *      string: ExpHolders
 *      uint64: subid
 *     list stop mark
 *    list stop mark
 *
 */
#define gulm_lock_login_req   (0x674C4C00) /* gLL0 */
#define gulm_lock_login_rpl   (0x674C4C01) /* gLL1 */
#define gulm_lock_logout_req  (0x674C4C02) /* gLL2 */
#define gulm_lock_logout_rpl  (0x674C4C03) /* gLL3 */
#define gulm_lock_sel_lckspc  (0x674C5300) /* gLS0 */
#define gulm_lock_state_req   (0x674C5200) /* gLR0 */
#define gulm_lock_state_rpl   (0x674C5201) /* gLR1 */
#define gulm_lock_state_updt  (0x674C5255) /* gLRU */
#define gulm_lock_action_req  (0x674C4100) /* gLA0 */
#define gulm_lock_action_rpl  (0x674C4101) /* gLA1 */
#define gulm_lock_action_updt (0x674C4155) /* gLAU */
#define gulm_lock_update_rpl  (0x674c5552) /* gLUR */
#define gulm_lock_query_req   (0x674c5100) /* gLQ0 */
#define gulm_lock_query_rpl   (0x674c5101) /* gLQ1 */
#define gulm_lock_cb_state    (0x674C4300) /* gLC0 */
#define gulm_lock_cb_dropall  (0x674C4302) /* gLC2 */
#define gulm_lock_drop_exp    (0x674C454F) /* gLEO */
#define gulm_lock_expire      (0x674C4545) /* gLEE */
#define gulm_lock_dump_req    (0x674c4400) /* gLD0 */
#define gulm_lock_dump_rpl    (0x674c4401) /* gLD1 */
#define gulm_lock_rerunqueues (0x674c5251) /* gLRQ */

/* marks for the login */
#define gio_lck_st_Slave     (0x00)
#define gio_lck_st_Client    (0x01)

/* state change requests */
#define gio_lck_st_Unlock    (0x00)
#define gio_lck_st_Exclusive (0x01)
#define gio_lck_st_Deferred  (0x02)
#define gio_lck_st_Shared    (0x03)
/* actions */
#define gio_lck_st_Cancel    (0x09)
#define gio_lck_st_HoldLVB   (0x0b)
#define gio_lck_st_UnHoldLVB (0x0c)
#define gio_lck_st_SyncLVB   (0x0d)

/* flags */
#define gio_lck_fg_Do_CB       (0x00000001)
#define gio_lck_fg_Try         (0x00000002)
#define gio_lck_fg_Any         (0x00000004)
#define gio_lck_fg_NoExp       (0x00000008)
#define gio_lck_fg_hasLVB      (0x00000010)
#define gio_lck_fg_Cachable    (0x00000020)
#define gio_lck_fg_Piority     (0x00000040)
 /* this is just an idea, but it might be useful.  Basically just says to
  * not keep the exp hold, just drop this hold like a shared would be.
  */
#define gio_lck_fg_DropOnExp   (0x00000080)
 /* this is saved on each holder, basically, you are gonna ignore any
  * callbacks about this lock, so tell the server not to even bother
  * sending them.  A tiny performance boost by lowering the network load.
  */
#define gio_lck_fg_NoCallBacks (0x00000100)

#endif /*__gio_wiretypes_h__*/
/* vim: set ai cin et sw=3 ts=3 : */
