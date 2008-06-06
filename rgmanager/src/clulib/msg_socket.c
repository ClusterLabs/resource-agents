#define _MESSAGE_BUILD
#include <message.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <errno.h>
#include <sys/time.h>
#include <fdops.h>

/* Ripped from ccsd's setup_local_socket */
#define RGMGR_SOCK "/var/run/cluster/rgmanager.sk"

static msg_ops_t sock_msg_ops;

static void
set_cloexec(int sock)
{
	long sock_flags;

	sock_flags = fcntl(sock, F_GETFD);
	fcntl(sock, F_SETFD, sock_flags | FD_CLOEXEC);
}

static int
sock_connect(void)
{
	struct sockaddr_un sun;
	int sock = -1, error = 0;

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = PF_LOCAL;
	snprintf(sun.sun_path, sizeof(sun.sun_path), "%s", RGMGR_SOCK);

	sock = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (sock < 0) {
		error = errno;
		goto fail;
	}

	error = connect(sock, (struct sockaddr *)&sun, sizeof(sun));
	if (error < 0) {
		error = errno;
		close(sock);
		errno = error;
		sock = -1;
		goto fail;
	}

fail:

	return sock;
}


/**
  Wrapper around write(2)
 */
static int
sock_msg_send(msgctx_t *ctx, void *msg, size_t len)
{
	char buf[4096];
	int ret;
	local_msg_hdr_t *h = (local_msg_hdr_t *)buf;
	char *msgptr = (buf + sizeof(*h));

	if (!ctx)
		return -1;
	if (!(ctx->flags & SKF_WRITE))
		return -1;

	/* encapsulate ... ? */
	if ((len + sizeof(*h)) > sizeof(buf)) {
		errno = E2BIG;
		return -1;
	}

	h->msg_control = M_DATA;
	h->msg_len = len;
	memcpy(msgptr, msg, len);

	ret = _write_retry(ctx->u.local_info.sockfd, buf,
			    len + sizeof(*h), NULL);

	if (ret >= sizeof(*h))
		return (ret - (sizeof(*h)));

	errno = EAGAIN;
	return -1;
}


static int
peekypeeky(int fd)
{
	local_msg_hdr_t h;
	int ret;

	while ((ret = recv(fd, (void *)&h, sizeof(h), MSG_PEEK)) < 0) {
		if (errno == EINTR)
			continue;
		return -1;
	}

	if (ret == sizeof(h))
		return h.msg_control;

	if (ret == 0)
		/* Socket closed? */
		return M_CLOSE;

	/* XXX */
	printf("PROTOCOL ERROR: Invalid message\n");
	return M_CLOSE;
}


static int
sock_msg_wait(msgctx_t *ctx, int timeout)
{
	fd_set rfds;
	struct timeval tv = {0, 0};
	struct timeval *p = NULL;

	if (timeout >= 0) {
		tv.tv_sec = timeout;
		p = &tv;
	}

	FD_ZERO(&rfds);
	FD_SET(ctx->u.local_info.sockfd, &rfds);

	if (_select_retry(ctx->u.local_info.sockfd + 1, &rfds,
			  NULL, NULL, p) == 1) {
		return peekypeeky(ctx->u.local_info.sockfd);
	}

	return M_NONE;
}


static int
sock_msg_fd_set(msgctx_t *ctx, fd_set *fds, int *max)
{
	errno = EINVAL;
	if (ctx->type != MSG_SOCKET)
		return -1;
	if (!fds)
		return -1;

	if (ctx->u.local_info.sockfd >= 0) {
		FD_SET(ctx->u.local_info.sockfd, fds);
		if (ctx->u.local_info.sockfd > *max)
			*max = ctx->u.local_info.sockfd;
		return 0;
	}

	return -1;
}


static int
sock_msg_fd_isset(msgctx_t *ctx, fd_set *fds)
{
	errno = EINVAL;
	if (!fds || !ctx)
		return -1;
	if (ctx->type != MSG_SOCKET)
		return -1;

	if (ctx->u.local_info.sockfd >= 0 &&
	    FD_ISSET(ctx->u.local_info.sockfd, fds)) {
		return 1;
	}
	return 0;
}


int
sock_msg_fd_clr(msgctx_t *ctx, fd_set *fds)
{
	errno = EINVAL;
	if (!fds || !ctx)
		return -1;
	if (ctx->type != MSG_SOCKET)
		return -1;

	if (ctx->u.local_info.sockfd >= 0) {
	    	FD_CLR(ctx->u.local_info.sockfd, fds);
		return 1;
	}
	return 0;
}


static int
_local_msg_receive(msgctx_t *ctx, void *msg, size_t maxlen, int timeout)
{
	struct timeval tv = {0, 0};
	struct timeval *p = NULL;
	local_msg_hdr_t h;

	if (timeout > 0) {
		tv.tv_sec = timeout;
		p = &tv;
	}

	if (_read_retry(ctx->u.local_info.sockfd, &h, sizeof(h), p) < 0)
		return -1;

	if (maxlen < h.msg_len) {
		printf("WARNING: Buffer too small for message (%d vs %d)!\n",
			h.msg_len, (int)maxlen);
		h.msg_len = maxlen;
	}

	return _read_retry(ctx->u.local_info.sockfd, msg, h.msg_len, p);
}


/**
  Receive a message from a cluster-context.  This copies out the contents
  into the user-specified buffer, and does random other things.
 */
static int
sock_msg_receive(msgctx_t *ctx, void *msg, size_t maxlen, int timeout)
{
	int req;
	char priv_msg[4096];
	size_t priv_len = sizeof(priv_msg);

	errno = EINVAL;
	if (!msg || !maxlen)
		return -1;
	if (ctx->type != MSG_SOCKET)
		return -1;
	if (!(ctx->flags & SKF_READ))
		return -1;

	req = _local_msg_receive(ctx, priv_msg, priv_len, timeout);

	if (req == 0) {
		errno = ECONNRESET;
		return -1;
	}

	if (req < 0)
		return -1;

	/* Copy out. */
	priv_len = (priv_len < maxlen ? priv_len : maxlen);

	memcpy(msg, priv_msg, priv_len);
	return req;

	printf("%s: CODE PATH ERROR\n", __FUNCTION__);
	return -1;
}


/**
  Open a connection to the specified node ID.
  If the speficied node is 0, this connects via the socket in
  /var/run/cluster...
 */
int
sock_msg_open(int type, int nodeid, int port, msgctx_t *ctx, int timeout)
{
	errno = EINVAL;
	if (!ctx || ctx->type != MSG_SOCKET)
		return -1;
	if (type != MSG_SOCKET)
		return -1;

	if (nodeid != CMAN_NODEID_US)
		return -1;
	if ((ctx->u.local_info.sockfd = sock_connect()) < 0)
		return -1;
	ctx->flags = (SKF_READ | SKF_WRITE);
	return 0;
}


/**
  With a socket, the O/S cleans up the buffers for us.
 */
int
sock_msg_close(msgctx_t *ctx)
{
	errno = EINVAL;
	if (ctx->type != MSG_SOCKET)
		return -1;

	close(ctx->u.local_info.sockfd);
	ctx->u.local_info.sockfd = -1;
	ctx->flags = 0;
	ctx->type = MSG_NONE;
	return 0;
}


/**
  Accept a new pseudo-private connection coming in over the
  cluster socket.
 */
static int
sock_msg_accept(msgctx_t *listenctx, msgctx_t *acceptctx)
{
	errno = EINVAL;

	if (!listenctx || !acceptctx)
		return -1;
	if (listenctx->u.local_info.sockfd < 0)
		return -1;
	if (!(listenctx->flags & SKF_LISTEN))
		return -1;

	listenctx->ops->mo_init(acceptctx);
	acceptctx->u.local_info.sockfd =
		accept(listenctx->u.local_info.sockfd, NULL, NULL);

	if (acceptctx->u.local_info.sockfd < 0)
		return -1;

	set_cloexec(acceptctx->u.local_info.sockfd);

	acceptctx->flags = (SKF_READ | SKF_WRITE);
	return 0;
}


int
sock_msg_listen(int me, void *portp, msgctx_t **listen_ctx)
{
	int sock;
	struct sockaddr_un su;
	mode_t om;
	msgctx_t *ctx = NULL;
	char *path = (char *)portp;

	/* Set up cluster context */
	ctx = msg_new_ctx();
	if (!ctx)
		return -1;

	sock = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (sock < 0)
		return -1;

	set_cloexec(sock);
	unlink(RGMGR_SOCK);
	om = umask(077);
	su.sun_family = PF_LOCAL;
	snprintf(su.sun_path, sizeof(su.sun_path), "%s", path);

	if (bind(sock, &su, sizeof(su)) < 0) {
		umask(om);
		goto fail;
	}
	umask(om);

	if (listen(sock, SOMAXCONN) < 0)
		goto fail;

	ctx->type = MSG_SOCKET;
	ctx->u.local_info.sockfd = sock;
	ctx->flags = SKF_LISTEN;
	ctx->ops = &sock_msg_ops;
	*listen_ctx = ctx;
	return 0;
fail:
	if (ctx)
		free(ctx);
	if (sock >= 0)
		close(sock);
	return -1;
}


/* XXX INCOMPLETE - no local_ctx setup*/
int
sock_msg_init(msgctx_t *ctx)
{
	memset(ctx,0,sizeof(*ctx));
	ctx->type = MSG_SOCKET;
	ctx->u.local_info.sockfd = -1;
	ctx->ops = &sock_msg_ops;
	return 0;
}


void
sock_msg_print(msgctx_t *ctx)
{
	printf("Socket Message Context; fd = %d\n", ctx->u.local_info.sockfd);
}


/* XXX INCOMPLETE */
int
sock_msg_shutdown(void)
{
	return 0;
}


static msg_ops_t sock_msg_ops = {
	.mo_open = sock_msg_open,
	.mo_close = sock_msg_close,
	.mo_listen = sock_msg_listen,
	.mo_accept = sock_msg_accept,
	.mo_shutdown = sock_msg_shutdown,
	.mo_wait = sock_msg_wait,
	.mo_send = sock_msg_send,
	.mo_receive = sock_msg_receive,
	.mo_fd_set = sock_msg_fd_set,
	.mo_fd_isset = sock_msg_fd_isset,
	.mo_fd_clr = sock_msg_fd_clr,
	.mo_print = sock_msg_print,
	.mo_init = sock_msg_init
};
