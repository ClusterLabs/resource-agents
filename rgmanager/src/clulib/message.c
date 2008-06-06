#define _MESSAGE_BUILD
#include <message.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>

/* From msg_cluster */
int cluster_msg_init(msgctx_t *ctx);
int cluster_msg_listen(int me, void *, msgctx_t **ctx);
int cluster_msg_shutdown(void);

/* From msg_socket  */
int sock_msg_init(msgctx_t *ctx);
int sock_msg_listen(int me, void *, msgctx_t **ctx);
int sock_msg_shutdown(void);


/**
  Message sending API.  Sends to the cluster or a socket, depending on
  the context.
 */
int
msg_send(msgctx_t *ctx, void *msg, size_t len)
{
	errno = EINVAL;
	if (!ctx || !msg || !len)
		return -1;

	if (ctx->ops && ctx->ops->mo_send)
		return ctx->ops->mo_send(ctx, msg, len);
	errno = ENOSYS;
	return -1;
}


/* XXX get API for this ready */
int
msg_get_nodeid(msgctx_t *ctx)
{
	/* XXX */
	switch(ctx->type) {
	case MSG_CLUSTER:
		return ctx->u.cluster_info.nodeid;
	case MSG_SOCKET:
		return 0;
	default:
		break;
	}

	return -1;
}


int
msg_fd_set(msgctx_t *ctx, fd_set *fds, int *max)
{
	errno = EINVAL;
	if (!ctx)
		return -1;
	
	if (ctx->ops && ctx->ops->mo_fd_set)
		return ctx->ops->mo_fd_set(ctx, fds, max);
	errno = ENOSYS;
	return -1;
}


int
msg_fd_isset(msgctx_t *ctx, fd_set *fds)
{
	errno = EINVAL;
	if (!ctx)
		return -1;
	
	if (ctx->ops && ctx->ops->mo_fd_isset)
		return ctx->ops->mo_fd_isset(ctx, fds);
	errno = ENOSYS;
	return -1;
}


int
msg_fd_clr(msgctx_t *ctx, fd_set *fds)
{
	errno = EINVAL;
	if (!ctx)
		return -1;
	
	if (ctx->ops && ctx->ops->mo_fd_clr)
		return ctx->ops->mo_fd_clr(ctx, fds);
	errno = ENOSYS;
	return -1;
}


/**
  This polls the context for 'timeout' seconds waiting for data
  to become available.  Return codes are M_DATA, M_CLOSE, and M_OPEN

  M_DATA - data available
  M_OPEN - needs msg_accept(
  M_CLOSE - context / socket closed by remote host
  M_NONE - nothing available

  For the cluster connection, the return code could also map to one of
  the CMAN return codes

  M_STATECHANGE - node has changed state

 */
int
msg_wait(msgctx_t *ctx, int timeout)
{
	errno = EINVAL;
	if (!ctx)
		return -1;
	
	if (ctx->ops && ctx->ops->mo_wait)
		return ctx->ops->mo_wait(ctx, timeout);
	errno = ENOSYS;
	return -1;
}



int
msg_receive(msgctx_t *ctx, void *msg, size_t maxlen, int timeout)
{
	errno = EINVAL;
	if (!ctx)
		return -1;

	if (ctx->ops && ctx->ops->mo_receive)
		return ctx->ops->mo_receive(ctx, msg, maxlen, timeout);
	errno = ENOSYS;
	return -1;
}


/**
  Open a connection to the specified node ID.
  If the speficied node is 0, this connects via the socket in
  /var/run/cluster...
 */
int
msg_open(int type, int nodeid, int port, msgctx_t *ctx, int timeout)
{
	errno = EINVAL;
	if (!ctx)
		return -1;

	/* XXX SPECIAL CASE... ow. */
	switch(type) {
	case MSG_SOCKET:
		sock_msg_init(ctx);
		break;
	case MSG_CLUSTER:
		cluster_msg_init(ctx);
		break;
	default:
		return -1;
	}

	/* Record where this was called, in case we have to debug */
	ctx->sp = __builtin_return_address(0);

	if (ctx->ops && ctx->ops->mo_open)
		return ctx->ops->mo_open(ctx->type, nodeid, port, ctx, timeout);
	errno = ENOSYS;
	return -1;
}


/**
  Close a connection context (cluster or socket; it doesn't matter)
  In the case of a cluster context, we need to clear out the 
  receive queue and what-not.  This isn't a big deal.  Also, we
  need to tell the other end that we're done -- just in case it does
  not know yet ;)

  With a socket, the O/S cleans up the buffers for us.
 */
int
msg_close(msgctx_t *ctx)
{
	errno = EINVAL;
	if (!ctx)
		return -1;
	if (ctx->ops && ctx->ops->mo_close)
		return ctx->ops->mo_close(ctx);
	errno = ENOSYS;
	return -1;
}


int
msg_accept(msgctx_t *listenctx, msgctx_t *acceptctx)
{
	errno = EINVAL;
	if (!listenctx || !acceptctx)
		return -1;
	if (listenctx->ops && listenctx->ops->mo_accept)
		return listenctx->ops->mo_accept(listenctx, acceptctx);
	errno = ENOSYS;
	return -1;
}


/* XXX Special case */
int
msg_listen(int type, void *port, int me, msgctx_t **ctx)
{
	errno = EINVAL;
	if (!me)
		return -1;
	if (type == MSG_NONE)
		return -1;
	if (!ctx)
		return -1;

	if (type == MSG_CLUSTER) {
		return cluster_msg_listen(me, port, ctx);
	} else if (type == MSG_SOCKET) {
		return sock_msg_listen(me, port, ctx);
	}

	return -1;
}


void
msg_print(msgctx_t *ctx)
{
	if (!ctx) {
		printf("Attempt to call %s on NULL\n", __FUNCTION__);
		return;
	}

	if (ctx->ops && ctx->ops->mo_print)
		return ctx->ops->mo_print(ctx);

	printf("Warning: Attempt to call %s on uninitialized context %p\n",
	       __FUNCTION__, ctx);
	printf("  ctx->type = %d\n", ctx->type);
}


/* XXX INCOMPLETE */
int
msg_shutdown(void)
{
	sock_msg_shutdown();
	cluster_msg_shutdown();

	return 0;
}


inline int
msgctx_size(void)
{
	return sizeof(msgctx_t);
}


msgctx_t *
msg_new_ctx(void)
{
	msgctx_t *p;
	
	p = malloc(sizeof(msgctx_t));
	if (!p)
		return NULL;

	memset(p, 0, sizeof(p));
	p->type = MSG_NONE;

	return p;
}


void
msg_free_ctx(msgctx_t *dead)
{
	free(dead);
}
