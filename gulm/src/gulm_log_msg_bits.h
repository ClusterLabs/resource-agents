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
#ifndef __gulm_log_msg_bits_h__
#define __gulm_log_msg_bits_h__
/* log_msg bit flags
 * These got thier own file so I can easily include them in both user and
 * kernel space.
 * */
#define lgm_Always      (0x00000000) /*Print Message no matter what */
#define lgm_Network     (0x00000001)
#define lgm_Network2    (0x00000002)
#define lgm_Stomith     (0x00000004)
#define lgm_Heartbeat   (0x00000008)
#define lgm_locking     (0x00000010)
#define lgm_SockSetup   (0x00000020)
#define lgm_Forking     (0x00000040)
#define lgm_JIDMap      (0x00000080)
#define lgm_Subscribers (0x00000100)
#define lgm_LockUpdates (0x00000200)
#define lgm_LoginLoops  (0x00000400)
#define lgm_Network3    (0x00000800) /* unused. */
#define lgm_JIDUpdates  (0x00001000)
#define lgm_ServerState (0x00002000)
#define lgm_Parsing     (0x00004000) /* unused. */

#define lgm_ReallyAll   (0xffffffff)

#define lgm_BitFieldSize (32)

#endif /*__gulm_log_msg_bits_h__*/
