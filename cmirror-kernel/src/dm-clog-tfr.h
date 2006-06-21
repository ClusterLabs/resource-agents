/*
 * Copyright (C) 2006 Red Hat, Inc.
 *
 * This file is released under the LGPL.
 */

#ifndef __DM_CLOG_TFR_H__

#define DM_CLOG_CTR                    1
#define DM_CLOG_DTR                    2
#define DM_CLOG_PRESUSPEND             3
#define DM_CLOG_POSTSUSPEND            4
#define DM_CLOG_RESUME                 5
#define DM_CLOG_GET_REGION_SIZE        6
#define DM_CLOG_IS_CLEAN               7
#define DM_CLOG_IS_REMOTE_RECOVERING   8
#define DM_CLOG_IN_SYNC                9
#define DM_CLOG_FLUSH                 10
#define DM_CLOG_MARK_REGION           11
#define DM_CLOG_CLEAR_REGION          12
#define DM_CLOG_GET_RESYNC_WORK       13
#define DM_CLOG_SET_REGION_SYNC       14
#define DM_CLOG_GET_SYNC_COUNT        15
#define DM_CLOG_STATUS                16
#define DM_CLOG_GET_FAILURE_RESPONSE  17

struct clog_tfr {
	char uuid[MAX_NAME_LEN];
	int error;               /* Used by server to inform of errors */
	int request_type;
	int data_size;
	char data[0];
};


int dm_clog_consult_server(const char *uuid, int request_type,
			   char *data, int data_size,
			   char *rdata, int *rdata_size);

#endif /* __DM_CLOG_TFR_H__ */
