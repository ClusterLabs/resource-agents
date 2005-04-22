/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

/* This is the device interface for dlm, most users will use a library
 * interface.
 */

/* Version of the device interface */
#define DLM_DEVICE_VERSION_MAJOR 3
#define DLM_DEVICE_VERSION_MINOR 0
#define DLM_DEVICE_VERSION_PATCH 0

#ifndef __KERNEL
#define __user
#endif

/* struct passed to the lock write */
struct dlm_lock_params {
	uint8_t mode;
	uint16_t flags;
	uint32_t lkid;
	uint32_t parent;
	struct dlm_range range;
	uint8_t namelen;
        void __user *castparam;
	void __user *castaddr;
	void __user *bastparam;
        void __user *bastaddr;
	struct dlm_lksb __user *lksb;
	char lvb[DLM_LVB_LEN];
	char name[1];
};

struct dlm_lspace_params {
	uint32_t flags;
	uint32_t minor;
	char name[1];
};

struct dlm_write_request {
	uint32_t version[3];
	uint8_t cmd;

	union  {
		struct dlm_lock_params   lock;
		struct dlm_lspace_params lspace;
	} i;
};

/* struct read from the "device" fd,
   consists mainly of userspace pointers for the library to use */
struct dlm_lock_result {
	uint32_t length;
	void __user * user_astaddr;
	void __user * user_astparam;
	struct dlm_lksb __user * user_lksb;
	struct dlm_lksb lksb;
	uint8_t bast_mode;
	/* Offsets may be zero if no data is present */
	uint32_t lvb_offset;
};

/* Commands passed to the device */
#define DLM_USER_LOCK         1
#define DLM_USER_UNLOCK       2
#define DLM_USER_QUERY        3
#define DLM_USER_CREATE_LOCKSPACE  4
#define DLM_USER_REMOVE_LOCKSPACE  5

/* Arbitrary length restriction */
#define MAX_LS_NAME_LEN 64

/* Lockspace flags */
#define DLM_USER_LSFLG_AUTOFREE   1
#define DLM_USER_LSFLG_FORCEFREE  2

