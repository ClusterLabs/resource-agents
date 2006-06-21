/*
 * Copyright (C) 2006 Red Hat, Inc.
 *
 * This file is released under the LGPL.
 */

#include "dm-clog-tfr.h"

/*
 * Pre-allocated nominal request area for speed
 */
#define DM_CLOG_NOMINAL_REQUEST_SIZE 512
static char nominal_request[DM_CLOG_NOMINAL_REQUEST_SIZE];

static DECLARE_MUTEX(consult_server_lock);

/*
 * dm_clog_consult_server
 * @uuid: log's uuid (must be MAX_NAME_LEN in size)
 * @request_type:
 * @data: data to tx to the server
 * @data_size: size of data in bytes
 * @rdata: place to put return data from server
 * @rdata_size: value-result (amount of space given/amount of space used)
 *
 * Only one process at a time can communicate with the server.
 * Possible error return values:
 *   +XXX:       Server-side error
 *   -XXX:       Client-side error
 *   -ENOSPC:    Not enough space in rdata
 *   -ENOMEM:    Unable to allocate memory to complete request
 *   -ESRCH:     Unable to contact server
 *   EIO:        Server unable to commit request
 *
 * Returns: 0 on success, otherwise failure
 */
int dm_clog_consult_server(const char *uuid, int request_type,
			   char *data, int data_size,
			   char *rdata, int *rdata_size)
{
	int r = 0;
	struct clog_tfr *tfr = (struct clog_tfr *)nominal_request;

	mutex_lock(&consult_server_lock);
	if (data_size > (DM_CLOG_NOMINAL_REQUEST_SIZE - sizeof(*tfr)))
		/* FIXME: is kmalloc sufficient if we need this much space? */
		tfr = kmalloc(data_size + sizeof(*tfr), GFP_KERNEL);

	if (!tfr)
		return -ENOMEM;

	memcpy(tfr->uuid, uuid, MAX_NAME_LEN);
	tfr->request_type = request_type;
	tfr->data_size = data_size;

	/*
	 * FIXME: Send to server
	 */

	if (rdata) {
		/* FIXME: receive from server */
		if (tfr->error) {
			r = tfr->error;
		} else if (tfr->data_size > *rdata_size) {
			r = -ENOSPC;
		} else {
			*rdata_size = tfr->data_size;
			memcpy(rdata, tft->data, tfr->data_size);
		}
		/* FIXME:  If using netlink, we may wish to ack back */
	} else {
		/*
		 * FIXME: If we are using netlink, we may want an
		 * ack from the server to know that it got the
		 * request.  (Ack is implicit if we are receiving
		 * data.)
		 */
	}
	r = ENOSYS;

	mutex_unlock(&consult_server_lock);
	return r;
}
