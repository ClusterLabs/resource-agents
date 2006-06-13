#define _MESSAGE_BUILD
#include <message.h>
#include <stdio.h>
#include <pthread.h>
#include <libcman.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <stdlib.h>
#include <rg_types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/time.h>
#include <fdops.h>
#include <resgroup.h>



/* Ripped from ccsd's setup_local_socket */
#define RGMGR_SOCK "/var/run/cluster/rgmanager.sk"
#define MAX_CONTEXTS 32  /* Testing; production should be 1024-ish */

/* Context 0 is reserved for control messages */

/* Local-ish contexts */
static pthread_mutex_t context_lock = PTHREAD_MUTEX_INITIALIZER;
static msgctx_t *contexts[MAX_CONTEXTS];
static uint32_t context_index = 1;
static chandle_t *gch;
pthread_t comms_thread;
int thread_running;


#define is_established(ctx) \
	(((ctx->type == MSG_CLUSTER) && \
 	  (ctx->u.cluster_info.remote_ctx && ctx->u.cluster_info.local_ctx)) || \
	 ((ctx->type == MSG_SOCKET) && \
	  (ctx->u.local_info.sockfd != -1)))


static int
local_connect(void)
{
	struct sockaddr_un sun;
	int sock = -1, error = 0;

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = PF_LOCAL;
	snprintf(sun.sun_path, sizeof(sun.sun_path), RGMGR_SOCK);

	sock = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (sock < 0) {
		error = errno;
		goto fail;
	}

	error = connect(sock, (struct sockaddr *)&sun, sizeof(sun));
	if (error < 0) {
		error = errno;
		goto fail;
	}

	sock = error;
fail:

	return sock;
}


static int
send_cluster_message(msgctx_t *ctx, void *msg, size_t len)
{
	char buf[4096];
	cluster_msg_hdr_t *h = (void *)buf;
	int ret;
	char *msgptr = (buf + sizeof(*h));

	if ((len + sizeof(*h)) > sizeof(buf)) {
		errno = E2BIG;
		return -1;
	}

	h->msg_control = M_DATA;
	h->src_ctx = ctx->u.cluster_info.local_ctx;
	h->dest_ctx = ctx->u.cluster_info.remote_ctx;
	h->msg_port = ctx->u.cluster_info.port;
	memcpy(msgptr, msg, len);

	/*
	printf("sending cluster message, length = %d to nodeid %d port %d\n",
	       len + sizeof(*h), ctx->u.cluster_info.nodeid, ctx->u.cluster_info.port);
	 */

	pthread_mutex_lock(&gch->c_lock);
	h->src_nodeid = gch->c_nodeid;

	swab_cluster_msg_hdr_t(h);

	ret = cman_send_data(gch->c_cluster, (void *)h, len + sizeof(*h),
			       ctx->u.cluster_info.nodeid,
			       ctx->u.cluster_info.port, 0);

	pthread_mutex_unlock(&gch->c_lock);

	return len + sizeof(h);
}


/**
  Wrapper around write(2)
 */
static int
send_socket_message(msgctx_t *ctx, void *msg, size_t len)
{
	char buf[4096];
	local_msg_hdr_t *h = (local_msg_hdr_t *)buf;
	char *msgptr = (buf + sizeof(*h));

	/* encapsulate ... ? */
	if ((len + sizeof(*h)) > sizeof(buf)) {
		errno = E2BIG;
		return -1;
	}

	h->msg_control = M_DATA;
	h->msg_len = len;
	memcpy(msgptr, msg, len);

	return _write_retry(ctx->u.local_info.sockfd, msg, len + sizeof(*h), NULL);
}


/**
  Message sending API.  Sends to the cluster or a socket, depending on
  the context.
 */
int
msg_send(msgctx_t *ctx, void *msg, size_t len)
{
	if (!ctx || !msg || !len) {
		errno = EINVAL;
		return -1;
	}

	switch(ctx->type) {
	case MSG_CLUSTER:
		return send_cluster_message(ctx, msg, len);
	case MSG_SOCKET:
		return send_socket_message(ctx, msg, len);
	default:
		break;
	}

	errno = EINVAL;
	return -1;
}


/**
  Assign a (free) cluster context ID
 */
static int
assign_ctx(msgctx_t *ctx)
{
	int start;

	/* Assign context index */
	ctx->type = MSG_CLUSTER;

	pthread_mutex_lock(&context_lock);
	start = context_index++;
	if (context_index >= MAX_CONTEXTS || context_index <= 0)
		context_index = 1;
	do {
		if (contexts[context_index]) {
			++context_index;
			if (context_index >= MAX_CONTEXTS)
				context_index = 1;

			if (context_index == start) {
				pthread_mutex_unlock(&context_lock);
				errno = EAGAIN;
				return -1;
			}

			continue;
		}

		contexts[context_index] = ctx;
		ctx->u.cluster_info.local_ctx = context_index;

	} while (0);
	pthread_mutex_unlock(&context_lock);

	return 0;
}


/* See if anything's on the cluster socket.  If so, dispatch it
   on to the requisite queues
   XXX should be passed a connection arg! */
static int
poll_cluster_messages(int timeout)
{
	int ret = -1;
	fd_set rfds;
	int fd;
	struct timeval tv;
	struct timeval *p = NULL;

	if (timeout >= 0) {
		p = &tv;
		tv.tv_sec = tv.tv_usec = timeout;
	}
	printf("%s\n", __FUNCTION__);

	FD_ZERO(&rfds);

	//pthread_mutex_lock(&gch->c_lock);
	fd = cman_get_fd(gch->c_cluster);
	FD_SET(fd, &rfds);

	if (select(fd + 1, &rfds, NULL, NULL, p) == 1) {
		cman_dispatch(gch->c_cluster, 0);
		ret = 0;
	}
	//pthread_mutex_unlock(&gch->c_lock);

	return ret;
}


/**
  This is used to establish and tear down pseudo-private
  contexts which are shared with the cluster context.
 */
static int
cluster_send_control_msg(msgctx_t *ctx, int type)
{
	cluster_msg_hdr_t cm;

	cm.msg_control = (uint8_t)type;
	cm.src_nodeid = gch->c_nodeid;
	cm.dest_ctx = ctx->u.cluster_info.remote_ctx;
	cm.src_ctx = ctx->u.cluster_info.local_ctx;
	cm.msg_port = ctx->u.cluster_info.port;

	swab_cluster_msg_hdr_t(&cm);

	return (cman_send_data(gch->c_cluster, (void *)&cm, sizeof(cm),
			       ctx->u.cluster_info.nodeid,
			       ctx->u.cluster_info.port, 0));
}


/**
  Wait for a message on a context.
 */
static int
cluster_msg_wait(msgctx_t *ctx, int timeout)
{
	struct timespec ts = {0, 0};
	int req = M_NONE;
	struct timeval start;
	struct timeval now;
	

	if (timeout > 0)
		gettimeofday(&start, NULL);

	ts.tv_sec = !!timeout;

	pthread_mutex_lock(&ctx->u.cluster_info.mutex);
	while (1) {
		/* See if we dispatched any messages on to our queue */
		if (ctx->u.cluster_info.queue) {
			req = ctx->u.cluster_info.queue->message->msg_control;
			/*printf("Queue not empty CTX%d : %d\n",
			  	 ctx->u.cluster_info.local_ctx, req);*/
			break;
		}

		if (timeout == 0)
			break;

		/* Ok, someone else has the mutex on our FD.  Go to
	   	   sleep on a cond; maybe they'll wake us up */
		if (pthread_cond_timedwait(&ctx->u.cluster_info.cond,
		    			   &ctx->u.cluster_info.mutex,
		   			   &ts) < 0) {

			/* Mutex held */
			if (errno == ETIMEDOUT) {
				if (timeout < 0) {
					ts.tv_sec = 1;
					ts.tv_nsec = 0;
					continue;
				} 

				ts.tv_sec = !!timeout;

				/* Done */
				break;
			}
		}

		if (timeout > 0) {
			gettimeofday(&now, NULL);
			/* XXX imprecise */
			if (now.tv_sec - start.tv_sec > timeout)
				break;
		}
	}
	pthread_mutex_unlock(&ctx->u.cluster_info.mutex);

	return req;
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
local_msg_wait(msgctx_t *ctx, int timeout)
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


int
msg_get_nodeid(msgctx_t *ctx)
{
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
	int e;
	switch(ctx->type) {
	case MSG_CLUSTER:
		pthread_mutex_lock(&ctx->u.cluster_info.mutex);
		if (ctx->u.cluster_info.select_pipe[0] < 0) {
			if (pipe(ctx->u.cluster_info.select_pipe) < 0) {
				e = errno;
				pthread_mutex_unlock(&ctx->u.cluster_info.mutex);
				errno = e;
				return -1;
			}

			printf("%s: Created cluster CTX select pipe "
			       "rd=%d wr=%d\n", __FUNCTION__,
			       ctx->u.cluster_info.select_pipe[0],
			       ctx->u.cluster_info.select_pipe[1]);

		}

		e = ctx->u.cluster_info.select_pipe[0];
		printf("%s: cluster %d\n", __FUNCTION__,  e);
		FD_SET(e, fds);
		pthread_mutex_unlock(&ctx->u.cluster_info.mutex);

		if (e > *max)
			*max = e;
		return 0;

	case MSG_SOCKET:
		if (ctx->u.local_info.sockfd >= 0) {
			printf("%s: local %d\n", __FUNCTION__,
			       ctx->u.local_info.sockfd);
			FD_SET(ctx->u.local_info.sockfd, fds);

			if (ctx->u.local_info.sockfd > *max)
				*max = ctx->u.local_info.sockfd;
			return 0;
		}
		return -1;
	default:
		break;
	}

	return -1;
}


int
msg_fd_isset(msgctx_t *ctx, fd_set *fds)
{
	errno = EINVAL;

	if (!fds || !ctx)
		return -1;

	switch(ctx->type) {
	case MSG_CLUSTER:
		pthread_mutex_lock(&ctx->u.cluster_info.mutex);
		if (ctx->u.cluster_info.select_pipe[0] >= 0 &&
		    FD_ISSET(ctx->u.cluster_info.select_pipe[0], fds)) {
			pthread_mutex_unlock(&ctx->u.cluster_info.mutex);
			return 1;
		}
		pthread_mutex_unlock(&ctx->u.cluster_info.mutex);
		return 0;
	case MSG_SOCKET:
		if (ctx->u.local_info.sockfd >= 0 &&
		    FD_ISSET(ctx->u.local_info.sockfd, fds)) {
			return 1;
		}
		return 0;
	default:
		break;
	}

	return -1;
}


int
msg_fd_clr(msgctx_t *ctx, fd_set *fds)
{
	errno = EINVAL;

	if (!fds || !ctx)
		return -1;

	switch(ctx->type) {
	case MSG_CLUSTER:
		pthread_mutex_lock(&ctx->u.cluster_info.mutex);
		if (ctx->u.cluster_info.select_pipe[0] >= 0) {
		    	FD_CLR(ctx->u.cluster_info.select_pipe[0], fds);
			pthread_mutex_unlock(&ctx->u.cluster_info.mutex);
			return 1;
		}
		pthread_mutex_unlock(&ctx->u.cluster_info.mutex);
		return 0;
	case MSG_SOCKET:
		if (ctx->u.local_info.sockfd >= 0) {
		    	FD_CLR(ctx->u.local_info.sockfd, fds);
			return 1;
		}
		return 0;
	default:
		break;
	}

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

	if (!ctx) {
		errno = EINVAL;
		return -1;
	}
		
	switch(ctx->type) {
	case MSG_CLUSTER:
		return cluster_msg_wait(ctx, timeout);
	case MSG_SOCKET:
		return local_msg_wait(ctx, timeout);
	default:
		break;
	}

	errno = EINVAL;
	return -1;
}


int
_cluster_msg_receive(msgctx_t *ctx, void **msg, size_t *len)
{
	cluster_msg_hdr_t *m;
	msg_q_t *n;
	int ret = 0;

	if (msg)
		*msg = NULL;
	if (len)
		*len = 0;

	if (ctx->u.cluster_info.local_ctx < 0 ||
	    ctx->u.cluster_info.local_ctx >= MAX_CONTEXTS) {
		errno = EBADF;
		return -1;
	}

	/* trigger receive here */
	pthread_mutex_lock(&ctx->u.cluster_info.mutex);

	n = ctx->u.cluster_info.queue;
	if (n == NULL) {
		pthread_mutex_unlock(&ctx->u.cluster_info.mutex);
		errno = EAGAIN;
		return -1;
	}

	list_remove(&ctx->u.cluster_info.queue, n);

	pthread_mutex_unlock(&ctx->u.cluster_info.mutex);

	m = n->message;
	switch(m->msg_control) {
	case M_CLOSE:
		ctx->u.cluster_info.remote_ctx = 0;
		break;
	case M_OPEN_ACK:
		/* Response to new connection */
		ctx->u.cluster_info.remote_ctx = m->src_ctx;
		break;
	case M_DATA:
		/* Kill the message control structure */
		memmove(m, &m[1], n->len - sizeof(*m));
		if (msg)
			*msg = (void *)m;
		else {
			printf("Warning: dropping data message\n");
			free(m);
		}
		if (len)
			*len = (n->len - sizeof(*m));
		ret = (n->len - sizeof(*m));
		free(n);

		printf("Message received\n");
		return ret;
	case M_OPEN:
		/* Someone is trying to open a connection */
	default:
		/* ?!! */
		ret = -1;
		break;
	}

	free(m);
	free(n);

	return ret;
}


/**
  Receive a message from a cluster-context.  This copies out the contents
  into the user-specified buffer, and does random other things.
 */
static int
cluster_msg_receive(msgctx_t *ctx, void *msg, size_t maxlen, int timeout)
{
	int req;
	void *priv_msg;
	size_t priv_len;

	if (!msg || !maxlen) {
		errno = EINVAL;
		return -1;
	}

	req = cluster_msg_wait(ctx, timeout);

	switch (req) {
	case M_DATA:
		/* Copy out. */
		req = _cluster_msg_receive(ctx, &priv_msg, &priv_len);
		if (req < 0) {
			printf("Ruh roh!\n");
			return -1;
		}

		priv_len = (priv_len < maxlen ? priv_len : maxlen);

		memcpy(msg, priv_msg, priv_len);
		free(priv_msg);
		return req;
	case M_CLOSE:
		errno = ECONNRESET;
		return -1;
	case 0:
		/*printf("Nothing on queue\n");*/
		return 0;
	default:
		printf("PROTOCOL ERROR: Received %d\n", req);
		return -1;
	}

	printf("%s: CODE PATH ERROR\n", __FUNCTION__);
	return -1;
}


static int
_local_msg_receive(msgctx_t *ctx, void *msg, size_t maxlen, int timeout)
{
	struct timeval tv = {0, 0};
	struct timeval *p = NULL;
	local_msg_hdr_t h;

	if (timeout >= 0) {
		tv.tv_sec = timeout;
		p = &tv;
	}

	if (_read_retry(ctx->u.local_info.sockfd, &h, sizeof(h), p) < 0)
		return -1;

	if (maxlen < h.msg_len) {
		printf("WARNING: Buffer too small for message!\n");
		h.msg_len = maxlen;
	}

	return _read_retry(ctx->u.local_info.sockfd, msg, h.msg_len, p);
}


/**
  Receive a message from a cluster-context.  This copies out the contents
  into the user-specified buffer, and does random other things.
 */
static int
local_msg_receive(msgctx_t *ctx, void *msg, size_t maxlen, int timeout)
{
	int req;
	char priv_msg[4096];
	size_t priv_len;

	if (!msg || !maxlen) {
		errno = EINVAL;
		return -1;
	}

	switch (req) {
	case M_DATA:
		/* Copy out. */
		req = _local_msg_receive(ctx, priv_msg, priv_len, timeout);
		if (req <= 0)
			return -1;

		priv_len = (priv_len < maxlen ? priv_len : maxlen);

		memcpy(msg, priv_msg, priv_len);
		free(msg);
		return req;
	case M_CLOSE:
		errno = ECONNRESET;
		return -1;
	case 0:
		/*printf("Nothing on queue\n");*/
		return 0;
	default:
		printf("PROTOCOL ERROR: Received %d\n", req);
		return -1;
	}

	printf("%s: CODE PATH ERROR\n", __FUNCTION__);
	return -1;
}


int
msg_receive(msgctx_t *ctx, void *msg, size_t maxlen, int timeout)
{
	if (!ctx || !msg || !maxlen) {
		errno = EINVAL;
		return -1;
	}

	switch(ctx->type) {
	case MSG_CLUSTER:
		return cluster_msg_receive(ctx, msg, maxlen, timeout);
	case MSG_SOCKET:
		return local_msg_receive(ctx, msg, maxlen, timeout);
	default:
		break;
	}

	errno = EINVAL;
	return -1;
}


/**
  Open a connection to the specified node ID.
  If the speficied node is 0, this connects via the socket in
  /var/run/cluster...
 */
int
msg_open(int nodeid, int port, msgctx_t *ctx, int timeout)
{
	int t = 0;

	errno = EINVAL;
	if (!ctx)
		return -1;


	/*printf("Opening pseudo channel to node %d\n", nodeid);*/

	memset(ctx, 0, sizeof(*ctx));
	if (nodeid == CMAN_NODEID_US) {
		if ((ctx->u.local_info.sockfd = local_connect()) < 0) {
			return -1;
		}
		ctx->type = MSG_SOCKET;
		return 0;
	}

	ctx->type = MSG_CLUSTER;
	ctx->u.cluster_info.nodeid = nodeid;
	ctx->u.cluster_info.port = port;
	ctx->u.cluster_info.local_ctx = -1;
	ctx->u.cluster_info.remote_ctx = 0;
	ctx->u.cluster_info.queue = NULL;
	ctx->u.cluster_info.select_pipe[0] = -1;
	ctx->u.cluster_info.select_pipe[1] = -1;
	pthread_mutex_init(&ctx->u.cluster_info.mutex, NULL);
	pthread_cond_init(&ctx->u.cluster_info.cond, NULL);

	/* Assign context index */
	if (assign_ctx(ctx) < 0)
		return -1;

	//printf("  Local CTX: %d\n", ctx->u.cluster_info.local_ctx);

	/* Send open */
	
	//printf("  Sending control message M_OPEN\n");
	if (cluster_send_control_msg(ctx, M_OPEN) < 0) {
		return -1;
	}

	/* Ok, wait for a response */
	while (!is_established(ctx)) {
		++t;
		if (t > timeout) {
			msg_close(ctx);
			errno = ETIMEDOUT;
			return -1;
		}
			
		switch(msg_wait(ctx, 1)) {
		case M_OPEN_ACK:
			_cluster_msg_receive(ctx, NULL, NULL);
			break;
		case M_NONE:
			continue;
		default: 
			printf("PROTO ERROR: M_OPEN_ACK not received \n");
		}
	}

	/*	
	printf("  Remote CTX: %d\n",
	       ctx->u.cluster_info.remote_ctx);
	printf("  Pseudo channel established!\n");
	*/
	

	return 0;
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
	msg_q_t *n = NULL;

	if (!ctx) {
		errno = EINVAL;
		return -1;
	}

	switch (ctx->type) {
	case MSG_CLUSTER:
		if (ctx->u.cluster_info.local_ctx >= MAX_CONTEXTS) {
			errno = EINVAL;
			return -1;
		}
		pthread_mutex_lock(&context_lock);
		/* Other threads should not be able to see this again */
		if (contexts[ctx->u.cluster_info.local_ctx])
			contexts[ctx->u.cluster_info.local_ctx] = NULL;
		pthread_mutex_unlock(&context_lock);
		/* Clear receive queue */
		while ((n = ctx->u.cluster_info.queue) != NULL) {
			list_remove(&ctx->u.cluster_info.queue, n);
			free(n->message);
			free(n);
		}
		/* Send close message */
		if (ctx->u.cluster_info.remote_ctx != 0) {
			cluster_send_control_msg(ctx, M_CLOSE);
		}

		/* Close pipe if it's open */
		if (ctx->u.cluster_info.select_pipe[0] >= 0) {
			close(ctx->u.cluster_info.select_pipe[0]);
			ctx->u.cluster_info.select_pipe[0] = -1;
		}
		if (ctx->u.cluster_info.select_pipe[1] >= 0) {
			close(ctx->u.cluster_info.select_pipe[1]);
			ctx->u.cluster_info.select_pipe[1] = -1;
		}
		return 0;
	case MSG_SOCKET:
		close(ctx->u.local_info.sockfd);
		ctx->u.local_info.sockfd = -1;
		return 0;
	default:
		break;
	}

	errno = EINVAL;
	return -1;
}


/**
  Called by cman_dispatch to deal with messages coming across the
  cluster socket.  This function deals with fanning out the requests
  and putting them on the per-context queues.  We don't have
  the benefits of pre-configured buffers, so we need this.
 */
static void
process_cman_msg(cman_handle_t h, void *priv, char *buf, int len,
	    uint8_t port, int nodeid)
{
	cluster_msg_hdr_t *m = (cluster_msg_hdr_t *)buf;
	msg_q_t *node;
	msgctx_t *ctx;

	if (len < sizeof(*m)) {
		printf("Message too short.\n");
		return;
	}

	swab_cluster_msg_hdr_t(m);

#if 0
	printf("Processing ");
	switch(m->msg_control) {
	case M_NONE: 
		printf("M_NONE\n");
		break;
	case M_OPEN:
		printf("M_OPEN\n");
		break;
	case M_OPEN_ACK: 
		printf("M_OPEN_ACK\n");
		break;
	case M_DATA: 
		printf("M_DATA\n");
		break;
	case M_CLOSE: 
		printf("M_CLOSE\n");
		break;
	}

	printf("  Node ID: %d %d\n", m->src_nodeid, nodeid);
	printf("  Remote CTX: %d  Local CTX: %d\n", m->src_ctx, m->dest_ctx);
#endif

	if (m->dest_ctx >= MAX_CONTEXTS) {
		printf("Context invalid; ignoring\n");
		return;
	}

	while ((node = malloc(sizeof(*node))) == NULL) {
		sleep(1);
	}
	memset(node, 0, sizeof(*node));
	while ((node->message = malloc(len)) == NULL) {
		sleep(1);
	}
	memcpy(node->message, buf, len);
	node->len = len;

	pthread_mutex_lock(&context_lock);
	ctx = contexts[m->dest_ctx];
	if (!ctx) {
		/* We received a close for something we've already
		   detached from our list.  No big deal, just
		   ignore. */
		free(node->message);
		free(node);
		pthread_mutex_unlock(&context_lock);
		return;
	}

	pthread_mutex_lock(&ctx->u.cluster_info.mutex);
	list_insert(&ctx->u.cluster_info.queue, node);
	pthread_mutex_unlock(&ctx->u.cluster_info.mutex);
	/* If a select pipe was set up, wake it up */
	if (ctx->u.cluster_info.select_pipe[1] >= 0)
		write(ctx->u.cluster_info.select_pipe[1], "", 1);
	pthread_mutex_unlock(&context_lock);

	pthread_cond_signal(&ctx->u.cluster_info.cond);
}


/**
  Accept a new pseudo-private connection coming in over the
  cluster socket.
 */
static int
cluster_msg_accept(msgctx_t *listenctx, msgctx_t *acceptctx)
{
	errno = EINVAL;
	cluster_msg_hdr_t *m;
	msg_q_t *n;
	char done = 0;
	char foo;

	if (!listenctx || !acceptctx)
		return -1;
	if (listenctx->u.cluster_info.local_ctx != 0)
		return -1;

	pthread_mutex_lock(&listenctx->u.cluster_info.mutex);

	n = listenctx->u.cluster_info.queue;
	if (n == NULL) {
		pthread_mutex_unlock(&listenctx->u.cluster_info.mutex);
		errno = EAGAIN;
		return -1;
	}

	/* the OPEN should be the first thing on the list; this loop
	   is probably not necessary */
	list_do(&listenctx->u.cluster_info.queue, n) {

		m = n->message;
		switch(m->msg_control) {
		case M_OPEN:
			list_remove(&listenctx->u.cluster_info.queue, n);
			/*printf("Accepting connection from %d %d\n",
			  	 m->src_nodeid, m->src_ctx);*/

			/* New connection */
			pthread_mutex_init(&acceptctx->u.cluster_info.mutex,
					   NULL);
			pthread_cond_init(&acceptctx->u.cluster_info.cond,
					  NULL);
			acceptctx->u.cluster_info.queue = NULL;
			acceptctx->u.cluster_info.remote_ctx = m->src_ctx;
			acceptctx->u.cluster_info.nodeid = m->src_nodeid;
			acceptctx->u.cluster_info.port = m->msg_port;

			assign_ctx(acceptctx);
			cluster_send_control_msg(acceptctx, M_OPEN_ACK);

			if (listenctx->u.cluster_info.select_pipe[0] >= 0) {
				read(listenctx->u.cluster_info.select_pipe[0],
				     &foo, 1);
			}

			done = 1;
			free(m);
			free(n);

			break;
		case M_DATA:
			/* Data messages (i.e. from broadcast msgs) are
			   okay too!...  but we don't handle them here */
			break;
		default:
			/* Other message?! */
			printf("Odd... %d\n", m->msg_control);
			break;
		}

		if (done)
			break;

	} while (!list_done(&listenctx->u.cluster_info.queue, n));

	pthread_mutex_unlock(&listenctx->u.cluster_info.mutex);

	return 0;
}


/* XXX INCOMPLETE */
int
msg_accept(msgctx_t *listenctx, msgctx_t *acceptctx)
{
	switch(listenctx->type) {
	case MSG_CLUSTER:
		return cluster_msg_accept(listenctx, acceptctx);
	case MSG_SOCKET:
		return 0;
	default:
		break;
	}

	return -1;
}


static int
local_listener_sk(void)
{
	int sock;
	struct sockaddr_un su;
	mode_t om;

	sock = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (sock < 0)
		return -1;

	unlink(RGMGR_SOCK);
	om = umask(077);
	su.sun_family = PF_LOCAL;
	snprintf(su.sun_path, sizeof(su.sun_path), RGMGR_SOCK);

	if (bind(sock, &su, sizeof(su)) < 0) {
		umask(om);
		goto fail;
	}
	umask(om);

	if (listen(sock, SOMAXCONN) < 0)
		goto fail;

	return sock;
fail:
	if (sock >= 0)
		close(sock);
	return -1;
}


/**
  This waits for events on the cluster comms FD and
  dispatches them using cman_dispatch.  Initially,
  the design had no permanent threads, but that model
  proved difficult to implement correctly.
 */
static void *
cluster_comms_thread(void *arg)
{
	while (thread_running) {
		poll_cluster_messages(2);
	}

	return NULL;
}


/*
   Transliterates a CMAN event to a control message
 */
static void
process_cman_event(cman_handle_t handle, void *private, int reason, int arg)
{
	cluster_msg_hdr_t *msg;
	int *argp;
	msg_q_t *node;
	msgctx_t *ctx;

	/* Allocate queue node */
	while ((node = malloc(sizeof(*node))) == NULL) {
		sleep(1);
	}
	memset(node, 0, sizeof(*node));

	/* Allocate message: header + int (for arg) */
	while ((msg = malloc(sizeof(int) +
			     sizeof(cluster_msg_hdr_t))) == NULL) {
		sleep(1);
	}
	memset(msg, 0, sizeof(int)+sizeof(cluster_msg_hdr_t));


	switch(reason) {
#if defined(LIBCMAN_VERSION) && LIBCMAN_VERSION >= 2
	case CMAN_REASON_PORTOPENED:
		msg->msg_control = M_PORTOPENED;
		break;
	case CMAN_REASON_TRY_SHUTDOWN:
		msg->msg_control = M_TRY_SHUTDOWN;
		break;
#endif
	case CMAN_REASON_PORTCLOSED:
		msg->msg_control = M_PORTCLOSED;
		break;
	case CMAN_REASON_STATECHANGE:
		msg->msg_control = M_STATECHANGE;
		break;
	}

	argp = ((void *)msg + sizeof(cluster_msg_hdr_t));
	*argp = arg;

	node->len = sizeof(cluster_msg_hdr_t) + sizeof(int);
	node->message = msg;

	pthread_mutex_lock(&context_lock);
	ctx = contexts[0]; /* This is the cluster context... */
	if (!ctx) {
		/* We received a close for something we've already
		   detached from our list.  No big deal, just
		   ignore. */
		free(node->message);
		free(node);
		pthread_mutex_unlock(&context_lock);
		return;
	}

	pthread_mutex_lock(&ctx->u.cluster_info.mutex);
	list_insert(&ctx->u.cluster_info.queue, node);
	pthread_mutex_unlock(&ctx->u.cluster_info.mutex);
	/* If a select pipe was set up, wake it up */
	if (ctx->u.cluster_info.select_pipe[1] >= 0)
		write(ctx->u.cluster_info.select_pipe[1], "", 1);
	pthread_mutex_unlock(&context_lock);

	pthread_cond_signal(&ctx->u.cluster_info.cond);
}


/* XXX INCOMPLETE */
int
msg_init(chandle_t *ch)
{
	int e;
	pthread_attr_t attrs;
	msgctx_t *ctx;

	pthread_mutex_lock(&ch->c_lock);

	/* Set up local context */

	ctx = msg_new_ctx();
	if (!ctx) {
		pthread_mutex_unlock(&ch->c_lock);
		return -1;
	}

	ctx->type = MSG_SOCKET;
	ctx->u.local_info.sockfd = local_listener_sk();
	ctx->u.local_info.flags = SKF_LISTEN;

	ch->local_ctx = ctx;

	ctx = msg_new_ctx();

	if (!ctx) {
		pthread_mutex_unlock(&ch->c_lock);
		msg_free_ctx((msgctx_t *)ch->local_ctx);
		return -1;
	}

	gch = ch;

	if (cman_start_recv_data(ch->c_cluster, process_cman_msg,
				 RG_PORT) != 0) {
		e = errno;
		msg_close(ch->local_ctx);
		pthread_mutex_unlock(&ch->c_lock);
		msg_free_ctx((msgctx_t *)ch->local_ctx);
		msg_free_ctx((msgctx_t *)ch->cluster_ctx);
		errno = e;
		return -1;
	}

	if (cman_start_notification(ch->c_cluster, process_cman_event) != 0) {
		e = errno;
		msg_close(ch->local_ctx);
		pthread_mutex_unlock(&ch->c_lock);
		msg_free_ctx((msgctx_t *)ch->local_ctx);
		msg_free_ctx((msgctx_t *)ch->cluster_ctx);
		errno = e;
	}

	ch->cluster_ctx = ctx;
	pthread_mutex_unlock(&ch->c_lock);

	pthread_mutex_lock(&context_lock);

	memset(contexts, 0, sizeof(contexts));
	contexts[0] = ctx;

	ctx->type = MSG_CLUSTER;
	ctx->u.cluster_info.port = RG_PORT; /* port! */
	ctx->u.cluster_info.nodeid = 0; /* Broadcast! */
	ctx->u.cluster_info.select_pipe[0] = -1;
	ctx->u.cluster_info.select_pipe[1] = -1;
	pthread_mutex_init(&ctx->u.cluster_info.mutex, NULL);
	pthread_cond_init(&ctx->u.cluster_info.cond, NULL);
	pthread_mutex_unlock(&context_lock);

       	pthread_attr_init(&attrs);
       	pthread_attr_setinheritsched(&attrs, PTHREAD_INHERIT_SCHED);
       	pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED);

	thread_running = 1;	
	pthread_create(&comms_thread, &attrs, cluster_comms_thread, NULL);


	pthread_attr_destroy(&attrs);

	return 0;
}


int
msg_print_ctx(int ctx)
{
	if (!contexts[ctx])
		return -1;

	printf("Cluster Message Context %d\n", ctx);
	printf("  Node ID %d\n", contexts[ctx]->u.cluster_info.nodeid);
	printf("  Remote %d\n", contexts[ctx]->u.cluster_info.remote_ctx);
	return 0;
}


/* XXX INCOMPLETE */
int
msg_shutdown(chandle_t *ch)
{
	if (!ch) {
		errno = EINVAL;
		return -1;
	}

	while (pthread_kill(comms_thread, 0) == 0)
		sleep(1);

	pthread_mutex_lock(&ch->c_lock);

	/* xxx purge everything */
	msg_close(ch->local_ctx);
	cman_end_recv_data(ch->c_cluster);

	msg_free_ctx(ch->local_ctx);
	msg_free_ctx(ch->cluster_ctx);


	pthread_mutex_unlock(&ch->c_lock);

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
	
	printf("Alloc %d\n", sizeof(msgctx_t));
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

