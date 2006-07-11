/*
  Copyright Red Hat, Inc. 2004

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2, or (at your option) any
  later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to the
  Free Software Foundation, Inc.,  675 Mass Ave, Cambridge, 
  MA 02139, USA.
*/
//#define DEBUG
#include <rg_types.h>
#include <resgroup.h>
#include <rg_queue.h>
#include <platform.h>
#include <msgsimple.h>
#include <clulog.h>
#include <message.h>


void
build_message(SmMessageSt *msgp, int action, char *svcName, uint64_t target)
{
	msgp->sm_hdr.gh_magic = GENERIC_HDR_MAGIC;
	msgp->sm_hdr.gh_command = RG_ACTION_REQUEST;
	msgp->sm_hdr.gh_length = sizeof(*msgp);
	msgp->sm_data.d_action = action;
	strncpy(msgp->sm_data.d_svcName, svcName,
		sizeof(msgp->sm_data.d_svcName));
	msgp->sm_data.d_svcOwner = target;
	msgp->sm_data.d_ret = 0;

	swab_SmMessageSt(msgp);
}


void *
forwarding_thread(void *arg)
{
	rg_state_t rgs;
	request_t *req = (request_t *)arg;
	struct dlm_lksb lockp;
	msgctx_t ctx;
	SmMessageSt msg;

	if (rg_lock(req->rr_group, &lockp) != 0) {
		msg_close(req->rr_resp_ctx);
		msg_free_ctx(req->rr_resp_ctx);
		rq_free(req);
		pthread_exit(NULL);
	}

	if (get_rg_state(req->rr_group, &rgs) != 0) {
		rg_unlock(&lockp);
		msg_close(req->rr_resp_ctx);
		msg_free_ctx(req->rr_resp_ctx);
		rq_free(req);
		pthread_exit(NULL);
	}

	rg_unlock(&lockp);

	/* Construct message */
	build_message(&msg, req->rr_request, req->rr_group, req->rr_target);

	/* 
	clulog(LOG_DEBUG, "Forwarding %s request to %d\n",
	       rg_req_str(req->rr_request), (int)rgs.rs_owner);
	 */

	if (msg_open(MSG_CLUSTER, rgs.rs_owner, RG_PORT, &ctx, 10) < 0)  {
		msg_close(req->rr_resp_ctx);
		msg_free_ctx(req->rr_resp_ctx);
		rq_free(req);
		pthread_exit(NULL);
	}

	if (msg_send(&ctx, &msg, sizeof(msg)) != sizeof(msg)) {
		msg_close(&ctx);
		msg_close(req->rr_resp_ctx);
		msg_free_ctx(req->rr_resp_ctx);
		rq_free(req);
		pthread_exit(NULL);
	}

	if (msg_receive(&ctx, &msg, sizeof(msg),10) != sizeof(msg)) {
		msg_close(&ctx);
		msg_close(req->rr_resp_ctx);
		msg_free_ctx(req->rr_resp_ctx);
		rq_free(req);
		pthread_exit(NULL);
	}
	msg_close(&ctx);

	swab_SmMessageSt(&msg);
	send_response(msg.sm_data.d_ret, req);

	rq_free(req);

	pthread_exit(NULL);
}


void
forward_request(request_t *req)
{
	pthread_t newthread;
	pthread_attr_t attrs;

        pthread_attr_init(&attrs);
        pthread_attr_setinheritsched(&attrs, PTHREAD_INHERIT_SCHED);
        pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED);
	pthread_attr_setstacksize(&attrs, 262144);

	pthread_create(&newthread, &attrs, forwarding_thread, req);
        pthread_attr_destroy(&attrs);
}

