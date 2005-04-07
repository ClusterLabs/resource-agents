/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __local_req_h__
#define __local_req_h__

/* FIXME -- should this be in a file by itself */
#define LOCAL_CREATE_REQ        1
#define LOCAL_REMOVE_REQ        2
#define LOCAL_INVALIDATE_REQ    3
#define LOCAL_FULL_LIST_REQ     4
#define LOCAL_GSERV_LIST_REQ    5
#define LOCAL_SHUTDOWN_REQ      6
#define LOCAL_VALIDATE_REQ      7

#define LOCAL_SUCCESS_REPLY     0
/* This is so that gnbd_export knows that it can kill gnbd_clusterd */
#define LOCAL_RM_CLUSTER_REPLY  1024

#define MAX_WWID_SIZE 256 /* got this from scsi_id code */

/* FIXME -- is this used */
#define REPLY_ERR(x) (-((int)(x)))

extern int is_clustered;

struct info_req_s {
  uint64_t sectors;
  uint32_t timeout;
  uint8_t flags;
  char name[32];
  char path[1024];
  char unique_id[MAX_WWID_SIZE];
};
typedef struct info_req_s info_req_t;

struct name_req_s {
  char name[32];
};
typedef struct name_req_s name_req_t;

struct gserv_req_s {
  char node[65];
  uint32_t pid;
  char name[32];
};
typedef struct gserv_req_s gserv_req_t;

int accept_local_connection(int listening_sock);
int check_local_data_len(uint32_t req, int size);
void handle_local_request(int sock, uint32_t cmd, void *buf);

#endif /* __local_req_h__ */
