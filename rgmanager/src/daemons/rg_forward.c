//#define DEBUG
#include <rg_types.h>
#include <resgroup.h>
#include <rg_queue.h>
#include <platform.h>
#include <msgsimple.h>
#include <clulog.h>
#include <message.h>
#include <members.h>


struct fw_message {
	msgctx_t *ctx;
	SmMessageSt msg;
	int nodeid;
};


void
build_message(SmMessageSt *msgp, int action, char *svcName, int target,
	      int arg1, int arg2)
{
	msgp->sm_hdr.gh_magic = GENERIC_HDR_MAGIC;
	msgp->sm_hdr.gh_command = RG_ACTION_REQUEST;
	msgp->sm_hdr.gh_arg1 = arg1;
	msgp->sm_hdr.gh_arg2 = arg2;
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
	msgctx_t *ctx = NULL;
	cluster_member_list_t *m = NULL;
	SmMessageSt msg;
	int response_code = RG_EAGAIN, ret;
	int new_owner = 0, retries = 0;

	if (rg_lock(req->rr_group, &lockp) != 0) {
		clulog(LOG_WARNING, "FW: Forwarding failed; lock unavailable for %s\n",
		       req->rr_group);
		goto out_fail;
	}
	if (get_rg_state(req->rr_group, &rgs) != 0) {
		rg_unlock(&lockp);
		clulog(LOG_WARNING, "FW: Forwarding failed; state unavailable for %s\n",
		       req->rr_group);
		goto out_fail;
	}
	rg_unlock(&lockp);

	if (rgs.rs_owner == 0)
		rgs.rs_owner = req->rr_target;
	if (rgs.rs_owner == 0) {
		clulog(LOG_ERR, "FW: Attempt to forward to invalid node ID\n");
       		goto out_fail;
	}
	if (rgs.rs_owner == (uint32_t)my_id()) {
		clulog(LOG_WARNING, "BUG! Attempt to forward to myself!\n");
       		goto out_fail;
	}

	clulog(LOG_DEBUG, "FW: Forwarding %s request to %d\n",
	       rg_req_str(req->rr_request), rgs.rs_owner);

	ctx = msg_new_ctx();
	if (ctx == NULL) {
		clulog(LOG_DEBUG, "FW: Failed to allocate socket context: %s\n",
		       strerror(errno));
		goto out_fail;
	}

	/* Construct message */
	build_message(&msg, req->rr_request, req->rr_group, req->rr_target,
		      req->rr_arg0, req->rr_arg1);

	if (msg_open(MSG_CLUSTER, rgs.rs_owner, RG_PORT, ctx, 10) < 0) {
		clulog(LOG_DEBUG, "FW: Failed to open channel to %d CTX: %p\n",
		       rgs.rs_owner, ctx);
		goto out_fail;
	}
	if (msg_send(ctx, &msg, sizeof(msg)) < (int)sizeof(msg)) {
		clulog(LOG_DEBUG, "FW: Failed to send message to %d CTX: %p\n",
		       rgs.rs_owner, ctx);
		goto out_fail;
	}

        /*
	 * Ok, we're forwarding a message to another node.  Keep tabs on
	 * the node to make sure it doesn't die.  Basically, wake up every
	 * now and again to make sure it's still online.  If it isn't, send
	 * a response back to the caller.
	 */
	do {
		ret = msg_receive(ctx, &msg, sizeof(msg), 10);
		if (ret < (int)sizeof(msg)) {
			if (ret < 0 && errno == ETIMEDOUT) {
				m = member_list();
				if (!memb_online(m, rgs.rs_owner)) {
					response_code = RG_ENODE;
					goto out_fail;
				}
				free_member_list(m);
				m = NULL;
				continue;
			}

			if (ret == 0)
				continue;
		}
		break;
	} while(++retries < 60); /* old 600 second rule */

	swab_SmMessageSt(&msg);

	response_code = msg.sm_data.d_ret;
	new_owner = msg.sm_data.d_svcOwner;

out_fail:
	send_response(response_code, new_owner, req);
	msg_close(req->rr_resp_ctx);
	msg_free_ctx(req->rr_resp_ctx);

	if (ctx) {
		msg_close(ctx);
		msg_free_ctx(ctx);
	}
	if (m)
		free_member_list(m);

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



void *
forwarding_thread_v2(void *arg)
{
	msgctx_t *ctx = NULL, *resp_ctx = NULL;
	cluster_member_list_t *m = NULL;
	SmMessageSt *msgp = NULL, msg;
	int response_code = RG_EAGAIN, ret, target = -1;
	int retries = 0;
	struct fw_message *fwmsg = (struct fw_message *)arg;

	msgp = &fwmsg->msg;
	resp_ctx = fwmsg->ctx;
	target = fwmsg->nodeid;

	clulog(LOG_DEBUG, "FW: Forwarding SM request to %d\n",
	       target);

	ctx = msg_new_ctx();
	if (ctx == NULL) {
		clulog(LOG_DEBUG, "FW: Failed to allocate socket context: %s\n",
		       strerror(errno));
		goto out_fail;
	}
	if (msg_open(MSG_CLUSTER, target, RG_PORT, ctx, 10) < 0) {
		clulog(LOG_DEBUG, "FW: Failed to open channel to %d CTX: %p\n",
		       target, ctx);
		goto out_fail;
	}

	/* swap + send */
	swab_SmMessageSt(msgp);
	if (msg_send(ctx, msgp, sizeof(*msgp)) < (int)sizeof(*msgp)) {
		clulog(LOG_DEBUG, "FW: Failed to send message to %d CTX: %p\n",
		       target, ctx);
		goto out_fail;
	}


        /*
	 * Ok, we're forwarding a message to another node.  Keep tabs on
	 * the node to make sure it doesn't die.  Basically, wake up every
	 * now and again to make sure it's still online.  If it isn't, send
	 * a response back to the caller.
	 */
	do {
		ret = msg_receive(ctx, &msg, sizeof(msg), 10);
		if (ret < (int)sizeof(msg)) {
			if (ret < 0 && errno == ETIMEDOUT) {
				m = member_list();
				if (!memb_online(m, target)) {
					response_code = RG_ENODE;
					goto out_fail;
				}
				free_member_list(m);
				m = NULL;
				continue;
			}

			if (ret == 0)
				continue;
		}
		break;
	} while(++retries < 60); /* old 600 second rule */

	swab_SmMessageSt(&msg);

	response_code = msg.sm_data.d_ret;
	target = msg.sm_data.d_svcOwner;

out_fail:
	free(fwmsg); 

	if (resp_ctx) {
		send_ret(resp_ctx, msgp->sm_data.d_svcName, response_code,
			 msgp->sm_data.d_action, target);
		msg_close(resp_ctx);
		msg_free_ctx(resp_ctx);
	}

	if (ctx) {
		msg_close(ctx);
		msg_free_ctx(ctx);
	}
	if (m)
		free_member_list(m);

	pthread_exit(NULL);
}


void
forward_message(msgctx_t *ctx, void *msgp, int nodeid)
{
	pthread_t newthread;
	pthread_attr_t attrs;
	struct fw_message *fwmsg;

	fwmsg = malloc(sizeof(struct fw_message));
	if (!fwmsg) {
		msg_close(ctx);
		msg_free_ctx(ctx);
		return;
	}

	memcpy(&fwmsg->msg, msgp, sizeof(fwmsg->msg));
	fwmsg->ctx = ctx;
	fwmsg->nodeid = nodeid;

        pthread_attr_init(&attrs);
        pthread_attr_setinheritsched(&attrs, PTHREAD_INHERIT_SCHED);
        pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED);
	pthread_attr_setstacksize(&attrs, 262144);

	pthread_create(&newthread, &attrs, forwarding_thread_v2, fwmsg);
        pthread_attr_destroy(&attrs);
}
