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

#ifndef __lg_priv_h__
#define __lg_priv_h__
/* private details that we don't want to give the users of this lib access
 * to go here.
 */

#ifdef __linux__
#include <linux/kernel.h>
#include <linux/sched.h>
#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>
#endif /*__linux__*/

#include "xdr.h"
#include "gio_wiretypes.h"
#include "libgulm.h"

#define LGMAGIC (0x474d4354)

struct gulm_interface_s {
	/* since we've masked this to a void* to the users, it is a nice safty
	 * net to put a little magic in here so we know things stay good.
	 */
	uint32_t first_magic;

	/* WHAT IS YOUR NAME?!? */
	char *service_name;

	char *clusterID;

	uint16_t core_port;
	xdr_socket core_fd;
	xdr_enc_t *core_enc;
	xdr_dec_t *core_dec;
	struct semaphore core_sender;
	struct semaphore core_recver;
	int in_core_hm;

	uint16_t lock_port;
	xdr_socket lock_fd;
	xdr_enc_t *lock_enc;
	xdr_dec_t *lock_dec;
	struct semaphore lock_sender;
	struct semaphore lock_recver;
	int in_lock_hm;
	uint8_t lockspace[4];

	/* in the message recver func, we read data into these buffers and pass
	 * them to the callback function.  This way we avoid doinf mallocs and
	 * frees on every callback.
	 */
	uint16_t cfba_len;
	uint8_t *cfba;
	uint16_t cfbb_len;
	uint8_t *cfbb;
	uint16_t lfba_len;
	uint8_t *lfba;
	uint16_t lfbb_len;
	uint8_t *lfbb;

	uint32_t last_magic;
};
typedef struct gulm_interface_s gulm_interface_t;

#ifndef TRUE
#define TRUE (1)
#endif

#ifndef FALSE
#define FALSE (0)
#endif

#endif /*__lg_priv_h__*/
