/*
  Copyright Red Hat, Inc. 2004-2007

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License version 2 as published
  by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to the
  Free Software Foundation, Inc.,  675 Mass Ave, Cambridge, 
  MA 02139, USA.
*/
#ifndef _RG_QUEUE_H
#define _RG_QUEUE_H
#include <list.h>
#include <stdint.h>
#include <sys/time.h>
#include <unistd.h>
#include <message.h>


/** 
 * Resource Group thread request queue entry.
 */
typedef struct _request {
	list_head();				/** Next/prev pointers */
	char		rr_group[64];		/** Resource Group */
	uint32_t	rr_request;		/** Request */
	uint32_t	rr_errorcode;		/** Error condition */
	uint32_t	rr_orig_request;	/** Original request */
	uint32_t	rr_target;		/** Target node */
	uint32_t	rr_arg0;		/** Integer argument */
	uint32_t	rr_arg1;		/** Integer argument */
	uint32_t	rr_arg2;		/** Integer argument */
	uint32_t	rr_line;		/** Line no */
	msgctx_t *	rr_resp_ctx;		/** FD to send response */
	char 		*rr_file;		/** Who made req */
	time_t		rr_when;		/** time to execute */
} request_t;


int _rq_queue_request(request_t **queue, char *name, uint32_t request,
    		     uint32_t err, uint32_t oldreq, msgctx_t *ctx, time_t when,
    		     uint32_t target, uint32_t arg0, uint32_t arg1, char *file,
		     int line);

#define rq_queue_request(queue, name, request, err, oldreq,\
			 fd, when, target, arg0, arg1) \
	_rq_queue_request(queue, name, request, err, oldreq, fd, when, \
			 target, arg0, arg1, __FILE__, __LINE__)

request_t *rq_next_request(request_t **q);
int rq_queue_empty(request_t **q);
void rq_free(request_t *foo);

void forward_request(request_t *req);
void forward_message(msgctx_t *ctx, void *msg, int nodeid);


#endif
