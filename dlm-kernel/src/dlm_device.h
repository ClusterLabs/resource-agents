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

/* This is the device interface for dlm, most users will use a library
 * interface.
 */

/* Version of the device interface */
#define DLM_DEVICE_VERSION_MAJOR 2
#define DLM_DEVICE_VERSION_MINOR 0
#define DLM_DEVICE_VERSION_PATCH 0

/* struct passed to the lock write */
struct dlm_lock_params {
	uint32_t version[3];
	uint8_t cmd;
	uint8_t mode;
	uint16_t flags;
	uint32_t lkid;
	uint32_t parent;
	struct dlm_range range;
	uint8_t namelen;
        void *castparam;
	void *castaddr;
	void *bastparam;
        void *bastaddr;
        struct dlm_lksb *lksb;
	char name[1];
};


/* struct read from the "device" fd,
   consists mainly of userspace pointers for the library to use */
struct dlm_lock_result {
    	uint8_t cmd;
        void *astparam;
        void (*astaddr)(void *astparam);
        struct dlm_lksb *user_lksb;
        struct dlm_lksb lksb;  /* But this has real data in it */
        uint8_t bast_mode; /* Not yet used */
};

/* commands passed to the device */
#define DLM_USER_LOCK       1
#define DLM_USER_UNLOCK     2
#define DLM_USER_QUERY      3

/* Arbitrary length restriction */
#define MAX_LS_NAME_LEN 64

/* ioctls on the device */
#define DLM_CREATE_LOCKSPACE         _IOW('D', 0x01, char *)
#define DLM_RELEASE_LOCKSPACE        _IOW('D', 0x02, char *)
#define DLM_FORCE_RELEASE_LOCKSPACE  _IOW('D', 0x03, char *)
