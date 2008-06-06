#include <rg_queue.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <message.h>


int
_rq_queue_request(request_t **queue, char *name, uint32_t request,
		 uint32_t err, uint32_t oldreq, msgctx_t *ctx, time_t when,
		 uint32_t target, uint32_t arg0, uint32_t arg1, char *file,
		 int line)
{
	request_t *req;

	req = malloc(sizeof(*req));
	if (!req)
		return -1;

	if (name && strlen(name)) {
		strncpy(req->rr_group, name, sizeof(req->rr_group));
	}
	req->rr_request = request;
	req->rr_errorcode = err;
	req->rr_orig_request = oldreq;
	req->rr_resp_ctx = ctx;
	req->rr_target = target;
	req->rr_when = when;
	req->rr_arg0 = arg0;
	req->rr_arg1 = arg1;
	req->rr_file = file;
	req->rr_line = line;

	list_insert(queue, req);

	return 0;
}


void
rq_free(request_t *dead)
{
	if (!dead)
		return;

	free(dead);
}


request_t *
rq_next_request(request_t **queue)
{
	request_t *req = NULL;

	req = *queue;
	if (req)
		list_remove(queue, req);

	return req;
}


int
rq_queue_empty(request_t **queue)
{
	return !(*queue);
}
