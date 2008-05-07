/*
  Copyright Red Hat, Inc. 2006

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
#define _MESSAGE_BUILD
#include <message.h>
#include <stdio.h>
#include <pthread.h>
#include <libcman.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <signals.h>
#include <gettid.h>
#include <cman-private.h>
#include <clulog.h>

/* Ripped from ccsd's setup_local_socket */

int cluster_msg_close(msgctx_t *ctx);

/* Context 0 is reserved for control messages */

/* Local-ish contexts */
static pthread_mutex_t context_lock = PTHREAD_MUTEX_INITIALIZER;
static msgctx_t *contexts[MAX_CONTEXTS];
static int _me = 0;
pthread_t comms_thread;
int thread_running = 0;

#define is_established(ctx) \
	(((ctx->type == MSG_CLUSTER) && \
 	  (ctx->u.cluster_info.remote_ctx && \
	   ctx->u.cluster_info.local_ctx)) || \
	 ((ctx->type == MSG_SOCKET) && \
	  (ctx->u.local_info.sockfd != -1)))

static msg_ops_t cluster_msg_ops;
static void cluster_msg_print(msgctx_t *ctx);


#define proto_error(ctx, msg, str) \
do { \
	printf("<<< CUT HERE >>>\n"); \
	printf("[%d] PROTOCOL ERROR in %s: %s\n", gettid(), __FUNCTION__, str); \
	msg_print(ctx); \
	if (msg) { \
	printf("  msg->msg_control = %d\n", ((cluster_msg_hdr_t *)msg)->msg_control); \
	printf("  msg->src_ctx = %d\n", ((cluster_msg_hdr_t *)msg)->src_ctx); \
	printf("  msg->dest_ctx = %d\n",  ((cluster_msg_hdr_t *)msg)->dest_ctx); \
	printf("  msg->src_nodeid = %d\n",  ((cluster_msg_hdr_t *)msg)->src_nodeid); \
	printf("  msg->msg_port = %d\n",  ((cluster_msg_hdr_t *)msg)->msg_port); \
	} \
	printf(">>> CUT HERE <<<\n"); \
} while(0)



static int
cluster_msg_send(msgctx_t *ctx, void *msg, size_t len)
{
	char ALIGNED buf[4096];
	cluster_msg_hdr_t *h = (void *)buf;
	char *msgptr = (buf + sizeof(*h));
	int ret;

	errno = EINVAL;
	if (ctx->type != MSG_CLUSTER)
		return -1;
	if (!(ctx->flags & SKF_WRITE))
		return -1;
	if ((len + sizeof(*h)) > sizeof(buf)) {
		errno = E2BIG;
		return -1;
	}

	h->msg_control = M_DATA;
	h->dest_nodeid = ctx->u.cluster_info.nodeid;
	h->src_ctx = ctx->u.cluster_info.local_ctx;
	h->dest_ctx = ctx->u.cluster_info.remote_ctx;
	h->msg_port = ctx->u.cluster_info.port;
	memcpy(msgptr, msg, len);
	h->src_nodeid = _me;

	/*
	printf("sending cluster message, length = %d to nodeid %d port %d\n",
	       len + sizeof(*h), ctx->u.cluster_info.nodeid,
	       ctx->u.cluster_info.port);
	 */

	swab_cluster_msg_hdr_t(h);

	ret = cman_send_data_unlocked((void *)h, len + sizeof(*h), 0,
			       ctx->u.cluster_info.port,
			       ctx->u.cluster_info.nodeid);

	if (ret < 0)
		return ret;

	if (ret >= (len + sizeof(*h)))
		return len;

	errno = EAGAIN;
	return -1;
}


/**
  Assign a (free) cluster context ID if possible
 */
static int
assign_ctx(msgctx_t *ctx)
{
	int start;
	static uint32_t context_index = 1;

	/* Assign context index */
	pthread_mutex_lock(&context_lock);

	start = context_index;
	do {
		context_index++;
		if (context_index >= MAX_CONTEXTS)
			context_index = 1;

		if (!contexts[context_index]) {
			contexts[context_index] = ctx;
			ctx->u.cluster_info.local_ctx = context_index;
			pthread_mutex_unlock(&context_lock);
			return 0;
		}
	} while (context_index != start);
	
	pthread_mutex_unlock(&context_lock);

	errno = EAGAIN;
	return -1;
}


/* See if anything's on the cluster socket.  If so, dispatch it
   on to the requisite queues
   XXX should be passed a connection arg! */
static int
poll_cluster_messages(int timeout)
{
	int ret = -1;
	fd_set rfds;
	int fd, lfd, max;
	struct timeval tv;
	struct timeval *p = NULL;
	cman_handle_t ch;

	if (timeout >= 0) {
		p = &tv;
		tv.tv_sec = tv.tv_usec = timeout;
	}

	FD_ZERO(&rfds);

	/* This sucks - it could cause other threads trying to get a
	   membership list to block for a long time.  Now, that should not
	   happen.  Basically, when we get a membership event, we generate 
	   a new membership list in a locally cached copy and reference
	   that.

	 */
	ch = cman_lock_preemptible(1, &lfd);
	if (!ch) {
		printf("%s\n", strerror(errno));
	}

	fd = cman_get_fd(ch);
	if (fd < 0) {
		cman_unlock(ch);
		return 0;
	}
	FD_SET(fd, &rfds);
	FD_SET(lfd, &rfds);

	max = (lfd > fd ? lfd : fd);
	if (select(max + 1, &rfds, NULL, NULL, p) > 0) {
		/* Someone woke us up */
		if (FD_ISSET(lfd, &rfds)) {
			cman_unlock(ch);
			errno = EAGAIN;
			return -1;
		}

		cman_dispatch(ch, 0);
		ret = 0;
	}
	cman_unlock(ch);

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
	int ret;

	cm.msg_control = (uint8_t)type;
	cm.dest_nodeid = ctx->u.cluster_info.nodeid;
	cm.src_nodeid = _me;
	cm.dest_ctx = ctx->u.cluster_info.remote_ctx;
	cm.src_ctx = ctx->u.cluster_info.local_ctx;
	cm.msg_port = ctx->u.cluster_info.port;

	swab_cluster_msg_hdr_t(&cm);

	ret = (cman_send_data_unlocked((void *)&cm, sizeof(cm), 0,
			       ctx->u.cluster_info.port,
			       ctx->u.cluster_info.nodeid) );
	return ret;
}


/**
  Wait for a message on a context.
 */
static int
cluster_msg_wait(msgctx_t *ctx, int timeout)
{
	struct timespec ts = {0, 0};
	struct timeval tv = {0, 0};
	int req = M_NONE;
	int e;

	errno = EINVAL;
	if (!ctx)
		return -1;
	if (ctx->type != MSG_CLUSTER)
		return -1;
	if (!(ctx->flags & (SKF_READ | SKF_LISTEN)))
		return -1;

	if (timeout > 0) {
		gettimeofday(&tv, NULL);
		ts.tv_sec = tv.tv_sec + timeout;
		ts.tv_nsec = tv.tv_usec * 1000;
	}

	pthread_mutex_lock(&ctx->u.cluster_info.mutex);
	while (1) {

		/* See if we dispatched any messages on to our queue */
		if (ctx->u.cluster_info.queue) {
			req = ctx->u.cluster_info.queue->message->msg_control;
			/*printf("Queue not empty CTX%d : %d\n",
			  	 ctx->u.cluster_info.local_ctx, req);*/
			break;
		}

		/* Ok, someone else has the mutex on our FD.  Go to
	   	   sleep on a cond; maybe they'll wake us up */
		e = pthread_cond_timedwait(&ctx->u.cluster_info.cond,
		    			   &ctx->u.cluster_info.mutex,
		   			   &ts);

		if (timeout == 0) {
			break;
		}

		if (e == 0) {
			continue;
		}

		if (e == ETIMEDOUT) {
			break;
		}
	}
	pthread_mutex_unlock(&ctx->u.cluster_info.mutex);

	return req;
}


static int
cluster_msg_fd_set(msgctx_t *ctx, fd_set *fds, int *max)
{
	int e;
	msg_q_t *n;
	
	errno = EINVAL;
	if (!ctx || !fds)
		return -1;
	if (ctx->type != MSG_CLUSTER)
		return -1;

	pthread_mutex_lock(&ctx->u.cluster_info.mutex);
	if (ctx->u.cluster_info.select_pipe[0] < 0) {
		if (pipe(ctx->u.cluster_info.select_pipe) < 0) {
			e = errno;
			pthread_mutex_unlock(&ctx->u.cluster_info.mutex);
			errno = e;
			return -1;
		}

		/*
		printf("%s: Created cluster CTX select pipe "
		       "rd=%d wr=%d\n", __FUNCTION__,
		       ctx->u.cluster_info.select_pipe[0],
		       ctx->u.cluster_info.select_pipe[1]);
		 */

		/* Ok, we just created the pipe.  Now, we need to write
		   a char for every unprocessed event to the pipe, because
		   events could be pending that would otherwise be unhandled
		   by the caller because the caller is switching to select()
		   semantics. (as opposed to msg_wait() ) */
		list_do(&ctx->u.cluster_info.queue, n) {
			if (write(ctx->u.cluster_info.select_pipe[1], "", 1) < 0) {
				e = errno;
				pthread_mutex_unlock(&ctx->u.cluster_info.mutex);
				errno = e;
				return -1;
			}
		} while (!list_done(&ctx->u.cluster_info.queue, n));
	}

	e = ctx->u.cluster_info.select_pipe[0];
	//printf("%s: cluster %d\n", __FUNCTION__,  e);
	FD_SET(e, fds);

	pthread_mutex_unlock(&ctx->u.cluster_info.mutex);

	if (max && (e > *max))
		*max = e;
	return 0;
}


int
cluster_msg_fd_isset(msgctx_t *ctx, fd_set *fds)
{
	errno = EINVAL;

	if (!fds || !ctx)
		return -1;

	if (ctx->type != MSG_CLUSTER)
		return -1;

	pthread_mutex_lock(&ctx->u.cluster_info.mutex);
	if (ctx->u.cluster_info.select_pipe[0] >= 0 &&
	    FD_ISSET(ctx->u.cluster_info.select_pipe[0], fds)) {
		pthread_mutex_unlock(&ctx->u.cluster_info.mutex);
		return 1;
	}
	pthread_mutex_unlock(&ctx->u.cluster_info.mutex);
	return 0;
}


int
cluster_msg_fd_clr(msgctx_t *ctx, fd_set *fds)
{
	errno = EINVAL;

	if (!fds || !ctx)
		return -1;

	if (ctx->type != MSG_CLUSTER)
		return -1;

	pthread_mutex_lock(&ctx->u.cluster_info.mutex);
	if (ctx->u.cluster_info.select_pipe[0] >= 0) {
	    	FD_CLR(ctx->u.cluster_info.select_pipe[0], fds);
		pthread_mutex_unlock(&ctx->u.cluster_info.mutex);
		return 1;
	}
	pthread_mutex_unlock(&ctx->u.cluster_info.mutex);
	return 0;
}


static int
_cluster_msg_receive(msgctx_t *ctx, void **msg, size_t *len)
{
	cluster_msg_hdr_t *m;
	msg_q_t *n;
	int ret = 0;
	char foo;

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

	if (ctx->u.cluster_info.select_pipe[0] >= 0) {
		//printf("%s read\n", __FUNCTION__);
		if (read(ctx->u.cluster_info.select_pipe[0], &foo, 1) < 0) {
			pthread_mutex_unlock(&ctx->u.cluster_info.mutex);
			return -1;
		}
	}

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

		//printf("Message received\n");
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
	msg_q_t *n;
	void *priv_msg;
	size_t priv_len;
	char foo;

	errno = EINVAL;
	if (!ctx)
		return -1;
	if (!(ctx->flags & SKF_READ))
		return -1;

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

		if (msg && maxlen)
			memcpy(msg, priv_msg, priv_len);
		free(priv_msg);
		return req;
	case M_CLOSE:
		errno = ECONNRESET;
		return -1;
	case 0:
		//printf("Nothing on queue\n");
		return 0;
	case M_STATECHANGE:
	case M_PORTOPENED:
	case M_PORTCLOSED:
	case M_TRY_SHUTDOWN:
		pthread_mutex_lock(&ctx->u.cluster_info.mutex);

		n = ctx->u.cluster_info.queue;
		if (n == NULL) {
			pthread_mutex_unlock(&ctx->u.cluster_info.mutex);
			errno = EAGAIN;
			return -1;
		}

		list_remove(&ctx->u.cluster_info.queue, n);

		if (ctx->u.cluster_info.select_pipe[0] >= 0) {
			//printf("%s read\n", __FUNCTION__);
			if (read(ctx->u.cluster_info.select_pipe[0], &foo, 1) < 0) {
				pthread_mutex_unlock(&ctx->u.cluster_info.mutex);
				return -1;
			}
		}
	
		pthread_mutex_unlock(&ctx->u.cluster_info.mutex);

		if (n->message)
			free(n->message);
		free(n);
		return 0;
	default:
		pthread_mutex_lock(&ctx->u.cluster_info.mutex);
		n = ctx->u.cluster_info.queue;
		list_remove(&ctx->u.cluster_info.queue, n);
		pthread_mutex_unlock(&ctx->u.cluster_info.mutex);

		proto_error(ctx, n->message, "Illegal request on established pchannel");
		if (n->message)
			free(n->message);
		free(n);
		return -1;
	}

	printf("%s: CODE PATH ERROR\n", __FUNCTION__);
	return -1;
}


/**
  Open a connection to the specified node ID.
  If the speficied node is 0, this connects via the socket in
  /var/run/cluster...
 */
static int
cluster_msg_open(int type, int nodeid, int port, msgctx_t *ctx, int timeout)
{
	int t = 0, ret;

	errno = EINVAL;
	if (!ctx)
		return -1;

	if (type != MSG_CLUSTER)
		return -1;

	/*printf("Opening pseudo channel to node %d\n", nodeid);*/
	
	ctx->type = MSG_CLUSTER;
	ctx->ops = &cluster_msg_ops;
	ctx->flags = 0;
	ctx->u.cluster_info.nodeid = nodeid;
	ctx->u.cluster_info.port = port;
	ctx->u.cluster_info.local_ctx = -1;
	ctx->u.cluster_info.remote_ctx = 0;
	ctx->u.cluster_info.queue = NULL;

	pthread_mutex_init(&ctx->u.cluster_info.mutex, NULL);
	pthread_cond_init(&ctx->u.cluster_info.cond, NULL);

	/* Assign context index */
	if (assign_ctx(ctx) < 0) {
		errno = EAGAIN;
		return -1;
	}
	ctx->flags = SKF_READ | SKF_WRITE;

	if (nodeid == CMAN_NODEID_US) {
		/* Broadcast pseudo ctx; no handshake needed */
		ctx->flags |= SKF_MCAST;
		return 0;
	}

	//printf("  Local CTX: %d\n", ctx->u.cluster_info.local_ctx);

	/* Send open */
	//printf("  Sending control message M_OPEN\n");
	if (cluster_send_control_msg(ctx, M_OPEN) < 0) {
		printf("Error sending control message: %s\n", strerror(errno));
		cluster_msg_close(ctx);
		return -1;
	}

	/* Ok, wait for a response */
	while (!is_established(ctx)) {
		++t;
		if (t > timeout) {
			cluster_msg_close(ctx);
			errno = ETIMEDOUT;
			return -1;
		}
			
		ret = cluster_msg_wait(ctx, 1);
		switch(ret) {
		case M_OPEN_ACK:
			_cluster_msg_receive(ctx, NULL, NULL);
			break;
		case M_NONE:
			continue;
		default: 
			proto_error(ctx, NULL, "M_OPEN_ACK not received\n");
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
cluster_msg_close(msgctx_t *ctx)
{
	msg_q_t *n = NULL;

	errno = EINVAL;

	if (!ctx)
		return -1;
	if (ctx->type != MSG_CLUSTER)
		return -1;

	if (ctx->u.cluster_info.local_ctx >= MAX_CONTEXTS) {
		printf("Context invalid during close\n");
		return -1;
	}


	pthread_mutex_lock(&context_lock);
	/* Other threads should not be able to see this again */
	if (contexts[ctx->u.cluster_info.local_ctx] &&
	    (contexts[ctx->u.cluster_info.local_ctx]->u.cluster_info.local_ctx ==
                ctx->u.cluster_info.local_ctx)) {
		//printf("reclaimed context %d\n", 
			//ctx->u.cluster_info.local_ctx);
		contexts[ctx->u.cluster_info.local_ctx] = NULL;
	}
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
	ctx->type = MSG_NONE;
	ctx->ops = NULL;


	return 0;
}


static void 
queue_for_context(msgctx_t *ctx, char *buf, int len)
{
	msg_q_t *node;

	if (ctx->type != MSG_CLUSTER) {
		clulog(LOG_WARNING, "%s called on invalid context %p\n",
		       __FUNCTION__, ctx);
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

	pthread_mutex_lock(&ctx->u.cluster_info.mutex);
	list_insert(&ctx->u.cluster_info.queue, node);
	/* If a select pipe was set up, wake it up */
	if (ctx->u.cluster_info.select_pipe[1] >= 0) {
		//printf("QUEUE_FOR_CONTEXT write\n");
		if (write(ctx->u.cluster_info.select_pipe[1], "", 1) < 0)
			perror("queue_for_context write");
	}
	pthread_mutex_unlock(&ctx->u.cluster_info.mutex);
	pthread_cond_signal(&ctx->u.cluster_info.cond);
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
	int x;

	if (len < sizeof(*m)) {
		printf("Message too short.\n");
		return;
	}

	swab_cluster_msg_hdr_t(m);

#ifdef DEBUG
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

	if (m->dest_ctx >= MAX_CONTEXTS || m->dest_ctx < 0) {
		printf("Context invalid; ignoring\n");
		return;
	}

	if (m->dest_nodeid != 0 && m->dest_nodeid != _me) {
#ifdef DEBUG
		printf("Skipping message meant for node %d (I am %d)\n",
		       m->dest_nodeid, _me);
#endif
		return;
	}

	pthread_mutex_lock(&context_lock);

	if (m->dest_ctx == 0 && m->msg_control == M_DATA) {
		/* Copy & place on all broadcast queues if it's a broadcast
		   M_DATA message... */
		for (x = 0; x < MAX_CONTEXTS; x++) {
			if (!contexts[x])
				continue;
			if (contexts[x]->type != MSG_CLUSTER)
				continue;
			if (!(contexts[x]->flags & SKF_MCAST))
				continue;
			if (!(contexts[x]->flags & SKF_READ))
				continue;

			queue_for_context(contexts[x], buf, len);
		}
	} else if (contexts[m->dest_ctx]) {

#if 0
		if (m->msg_control == M_OPEN_ACK) {
			for (x = 0; x < MAX_CONTEXTS; x++) {
				if (contexts[x] &&
				    contexts[x]->dest_ctx == m->src_ctx) {
					proto_error(contexts[x], m,
						"Duplicate M_OPEN_ACK");
				}
			}
		}
#endif
		if (m->msg_control == M_CLOSE &&
		    contexts[m->dest_ctx]->type != MSG_CLUSTER) {
			/* XXX Work around bug where M_CLOSE is called
			   on a context which has been destroyed */
			clulog(LOG_WARNING, "Ignoring M_CLOSE for destroyed "
			       "context %d\n", m->dest_ctx);
		} else {
			queue_for_context(contexts[m->dest_ctx], buf, len);
		}
	}
	/* If none of the above, then we msg for something we've already
	   detached from our list.  No big deal, just ignore. */

	pthread_mutex_unlock(&context_lock);
	return;
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
	char foo;
	int err = 0;

	if (!listenctx || !acceptctx)
		return -1;
	if (listenctx->u.cluster_info.local_ctx != 0)
		return -1;
	if (!(listenctx->flags & SKF_LISTEN))
		return -1;

	listenctx->ops->mo_init(acceptctx);

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
			/* XXX make this case statement its own function or at 
			   least make it not a big case block . */
			list_remove(&listenctx->u.cluster_info.queue, n);
			/*printf("Accepting connection from %d %d\n",
			  	 m->src_nodeid, m->src_ctx);*/

			/* Release lock on listen context queue; we're done
			   with it at this point */
			pthread_mutex_unlock(&listenctx->u.cluster_info.mutex);

			/* New connection: first, create + lock the mutex */
			pthread_mutex_init(&acceptctx->u.cluster_info.mutex,
					   NULL);
			/* Lock this while we finish initializing */
			pthread_mutex_lock(&acceptctx->u.cluster_info.mutex);

			pthread_cond_init(&acceptctx->u.cluster_info.cond,
					  NULL);

			acceptctx->u.cluster_info.queue = NULL;
			acceptctx->u.cluster_info.remote_ctx = m->src_ctx;
			acceptctx->u.cluster_info.nodeid = m->src_nodeid;
			acceptctx->u.cluster_info.port = m->msg_port;
			acceptctx->flags = (SKF_READ | SKF_WRITE);

			/* assign_ctx requires the context lock.  We need to 
			ensure we don't try to take the context lock w/ a local
			queue lock held on a context that's in progress (i.e.
			the global cluster context...) */
			if (assign_ctx(acceptctx) < 0)
				printf("FAILED TO ASSIGN CONTEXT\n");

			cluster_send_control_msg(acceptctx, M_OPEN_ACK);

			if (listenctx->u.cluster_info.select_pipe[0] >= 0) {
				//printf("%s read\n", __FUNCTION__);
				if (read(listenctx->u.cluster_info.select_pipe[0], &foo, 1) < 0)
					err = -1;
			}

			free(m);
			free(n);

			/* Let the new context go. */
			pthread_mutex_unlock(&acceptctx->u.cluster_info.mutex);
			return err;
			/* notreached */

		case M_DATA:
			/* Data messages (i.e. from broadcast msgs) are
			   okay too!...  but we don't handle them here */
			break;
		default:
			/* Other message?! */
			printf("Odd... %d\n", m->msg_control);
			break;
		}

	} while (!list_done(&listenctx->u.cluster_info.queue, n));

	pthread_mutex_unlock(&listenctx->u.cluster_info.mutex);

	return 0;
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
	/* SIGUSR2 will cause select() to abort */
	while (thread_running) {
		poll_cluster_messages(2);
	}

	pthread_exit(NULL);
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

#if 0
	printf("EVENT: %p %p %d %d\n", handle, private, reason, arg);
#endif

	/* Allocate queue node */
	while ((node = malloc(sizeof(*node))) == NULL) {
		sleep(1);
	}
	memset(node, 0, sizeof(*node));

	/* Allocate message: header + int (for arg) */
	while ((msg = malloc(sizeof(int)*2 +
			     sizeof(cluster_msg_hdr_t))) == NULL) {
		sleep(1);
	}
	memset(msg, 0, sizeof(int)*2 +sizeof(cluster_msg_hdr_t));

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
	pthread_mutex_unlock(&context_lock);

	pthread_mutex_lock(&ctx->u.cluster_info.mutex);
	list_insert(&ctx->u.cluster_info.queue, node);
	/* If a select pipe was set up, wake it up */
	if (ctx->u.cluster_info.select_pipe[1] >= 0) {
		//printf("PROCESS_CMAN_EVENT write\n");
		if (write(ctx->u.cluster_info.select_pipe[1], "", 1) < 0)
			perror("process_cman_event write");
	}
	pthread_mutex_unlock(&ctx->u.cluster_info.mutex);
	pthread_cond_signal(&ctx->u.cluster_info.cond);
}


/* */
int
cluster_msg_listen(int me, void *portp, msgctx_t **cluster_ctx)
{
	int e;
	pthread_attr_t attrs;
	cman_handle_t ch = NULL;
	msgctx_t *ctx;
	uint8_t port;

	errno = EINVAL;
	if (!portp)
		return -1;
	port = *(uint8_t *)portp;
	if (port < 10 || port > 254)
		return -1;

	ch = cman_lock(1, 0);
	_me = me;

	/* Set up cluster context */
	ctx = msg_new_ctx();
	if (!ctx) {
		cman_unlock(ch);
		errno = EINVAL;
		return -1;
	}

	memset(contexts, 0, sizeof(contexts));

	if (cman_start_recv_data(ch, process_cman_msg, port) != 0) {
		e = errno;
		cman_unlock(ch);
		msg_free_ctx((msgctx_t *)ctx);

		printf("Doom\n");
		errno = e;
		return -1;
	}

	if (cman_start_notification(ch, process_cman_event) != 0) {
		e = errno;
		cman_unlock(ch);
		msg_free_ctx((msgctx_t *)ctx);
		errno = e;
		return -1;
	}

	cman_unlock(ch);
	/* Done with CMAN bits */

	pthread_mutex_lock(&context_lock);

	memset(contexts, 0, sizeof(contexts));
	contexts[0] = ctx;

	ctx->type = MSG_CLUSTER;
	ctx->ops = &cluster_msg_ops;
	ctx->u.cluster_info.local_ctx = 0;
	ctx->u.cluster_info.remote_ctx = 0;
	ctx->u.cluster_info.port = port; /* port! */
	ctx->u.cluster_info.nodeid = 0; /* Broadcast! */
	ctx->u.cluster_info.select_pipe[0] = -1;
	ctx->u.cluster_info.select_pipe[1] = -1;
	ctx->u.cluster_info.queue = NULL;
	pthread_mutex_init(&ctx->u.cluster_info.mutex, NULL);
	pthread_cond_init(&ctx->u.cluster_info.cond, NULL);
	ctx->flags = SKF_LISTEN | SKF_READ | SKF_WRITE | SKF_MCAST;
	pthread_mutex_unlock(&context_lock);

	*cluster_ctx = ctx;

       	pthread_attr_init(&attrs);
       	pthread_attr_setinheritsched(&attrs, PTHREAD_INHERIT_SCHED);
       	/*pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED);*/

	thread_running = 1;	
	pthread_create(&comms_thread, &attrs, cluster_comms_thread, NULL);

	pthread_attr_destroy(&attrs);

	return 0;
}


static void
cluster_msg_print(msgctx_t *ctx)
{
	if (!ctx)
		return;

	printf("Cluster Message Context %p\n", ctx);
	printf("  Flags %08x\n", ctx->flags);
	printf("  Node ID %d\n", ctx->u.cluster_info.nodeid);
	printf("  Local CTX %d\n", ctx->u.cluster_info.local_ctx);
	printf("  Remote CTX %d\n", ctx->u.cluster_info.remote_ctx);
}


void
dump_cluster_ctx(FILE *fp)
{
	int x;
	msgctx_t *ctx;

	fprintf(fp, "CMAN/mux subsystem status\n");
	if (thread_running) {
		fprintf(fp, "  Thread: %d\n", (unsigned)comms_thread);
	} else {
		fprintf(fp, "  Thread Offline\n");
	}

	pthread_mutex_lock(&context_lock);
	for (x = 0; x < MAX_CONTEXTS; x++) {
		if (!contexts[x]) 
			continue;
		ctx = contexts[x];

		fprintf(fp, "    Cluster Message Context %p\n", ctx);
		fprintf(fp, "      Flags %08x  ", ctx->flags);
		if (ctx->flags & SKF_READ)
			fprintf(fp, "SKF_READ ");
		if (ctx->flags & SKF_WRITE)
			fprintf(fp, "SKF_WRITE ");
		if (ctx->flags & SKF_LISTEN)
			fprintf(fp, "SKF_LISTEN ");
		if (ctx->flags & SKF_MCAST)
			fprintf(fp, "SKF_MCAST ");
		fprintf(fp, "\n");
		fprintf(fp, "      Target node ID %d\n", ctx->u.cluster_info.nodeid);
		fprintf(fp, "      Local Index %d\n", ctx->u.cluster_info.local_ctx);
		fprintf(fp, "      Remote Index %d\n", ctx->u.cluster_info.remote_ctx);
	}
	pthread_mutex_unlock(&context_lock);
	fprintf(fp, "\n");
}


int
cluster_msg_shutdown(void)
{
	cman_handle_t ch;
	cluster_msg_hdr_t m;
	msgctx_t *ctx;
	int x;

	thread_running = 0;
	pthread_join(comms_thread, NULL);

	ch = cman_lock(1, SIGUSR2);
	cman_end_recv_data(ch);
	cman_unlock(ch);

	/* Send close message to all open contexts */
	memset(&m, 0, sizeof(m));
	m.msg_control = M_CLOSE;

	pthread_mutex_lock(&context_lock);
	for (x = 0; x < MAX_CONTEXTS; x++) {
		if (!contexts[x])
			continue;

		ctx = contexts[x];

		/* Kill remote side if it exists */
		if (is_established(ctx))
			cluster_send_control_msg(ctx, M_CLOSE);

		/* Queue close for local side */
		queue_for_context(ctx, (void *)&m, sizeof(m));
	}
	pthread_mutex_unlock(&context_lock);


	return 0;
}


int
cluster_msg_init(msgctx_t *ctx)
{
	errno = EINVAL;
	if (!ctx)
		return -1;

	memset(ctx, 0, sizeof(*ctx));
	ctx->type = MSG_CLUSTER;
	ctx->ops = &cluster_msg_ops;
	pthread_mutex_init(&ctx->u.cluster_info.mutex, NULL);
	pthread_cond_init(&ctx->u.cluster_info.cond, NULL);
	ctx->u.cluster_info.select_pipe[0] = -1;
	ctx->u.cluster_info.select_pipe[1] = -1;

	return 0;
}


static msg_ops_t cluster_msg_ops = {
	.mo_open = cluster_msg_open,
	.mo_close = cluster_msg_close,
	.mo_listen = cluster_msg_listen,
	.mo_accept = cluster_msg_accept,
	.mo_shutdown = cluster_msg_shutdown,
	.mo_wait = cluster_msg_wait,
	.mo_send = cluster_msg_send,
	.mo_receive = cluster_msg_receive,
	.mo_fd_set = cluster_msg_fd_set,
	.mo_fd_isset = cluster_msg_fd_isset,
	.mo_fd_clr = cluster_msg_fd_clr,
	.mo_print = cluster_msg_print,
	.mo_init = cluster_msg_init
};
