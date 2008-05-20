/*
  Copyright Red Hat, Inc. 2002-2006

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
/** @file
 * View-Formation Thread Library
 *
 * Similar to a two-phase commit.  This code is not especially optimal
 * for the kind of work it's doing in rgmanager (e.g. distributing 
 * resource group state).  It's probably better to use a client/server
 * model like NFS and have clients restate their resource group states
 * after a server failure.
 */
#include <platform.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <sys/time.h>
#include <pthread.h>
#include <vf.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <signals.h>
#include <lock.h>

static key_node_t *key_list = NULL;	/** List of key nodes. */
static int _node_id = (int)-1;/** Our node ID, set with vf_init. */
static uint16_t _port = 0;		/** Our daemon ID, set with vf_init. */

/*
 * TODO: We could make it thread safe, but this might be unnecessary work
 * Solution: Super-coarse-grained-bad-code-locking!
 */
#ifdef WRAP_LOCKS
static pthread_mutex_t key_list_mutex = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;
static pthread_mutex_t vf_mutex = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;
#else
static pthread_mutex_t key_list_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t vf_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif /* WRAP_LOCKS */
static pthread_t vf_thread = (pthread_t)-1;
static int vf_thread_ready = 0;
static vf_vote_cb_t default_vote_cb = NULL;
static vf_vote_cb_t default_commit_cb = NULL;


/*
 * Internal Functions
 */
static int _send_simple(msgctx_t *ctx, int32_t command, int arg1, int arg2,
		        int log_errors);
static int vf_send_abort(msgctx_t *ctx, uint32_t trans);
static int vf_send_commit(msgctx_t *ctx, uint32_t trans);
static key_node_t * kn_find_key(char *keyid);
static key_node_t * kn_find_trans(uint32_t trans);
static int vf_handle_join_view_msg(msgctx_t *ctx, int nodeid, vf_msg_t * hdrp);
static int vf_resolve_views(key_node_t *key_node);
static int vf_unanimous(msgctx_t *ctx, int trans, int remain, int timeout);
static view_node_t * vn_new(uint32_t trans, uint32_t nodeid, int viewno,
			    void *data, uint32_t datalen);
static int vf_request_current(cluster_member_list_t *membership, char *keyid,
		   	      uint64_t *viewno, void **data, uint32_t *datalen);
static int _vf_purge(key_node_t *key_node, uint32_t *trans);

/* Join-view buffer list functions */
static int vn_cmp(view_node_t *left, view_node_t *right);
static int vn_insert_sorted(view_node_t **head, view_node_t *node);
static view_node_t * vn_remove(view_node_t **head, uint32_t trans);
static int vf_buffer_join_msg(vf_msg_t *hdr,
			      struct timeval *timeout);

/* Commits buffer list functions */
static int vc_cmp(commit_node_t *left, commit_node_t *right);
static int vc_insert_sorted(commit_node_t **head, commit_node_t *node);
static commit_node_t * vc_remove(commit_node_t **head, uint32_t trans);
static int vf_buffer_commit(uint32_t trans);

/* Simple functions which client calls to vote/abort */
static int vf_vote_yes(msgctx_t *ctx, uint32_t trans);
static int vf_vote_no(msgctx_t *ctx, uint32_t trans);
static int vf_abort(uint32_t trans);
static int tv_cmp(struct timeval *left, struct timeval *right);

/* Resolution */
static uint32_t vf_try_commit(key_node_t *key_node);

int vf_init(int my_node_id, uint16_t my_port,
	    vf_vote_cb_t vote_cb, vf_commit_cb_t commit_cb);
int vf_key_init(char *keyid, int timeout, vf_vote_cb_t vote_cb,
		vf_commit_cb_t commit_cb);
static int vf_key_init_nt(char *keyid, int timeout, vf_vote_cb_t vote_cb,
			  vf_commit_cb_t commit_cb);
int vf_write(cluster_member_list_t *memberhip, uint32_t flags,
	     char *keyid, void *data, uint32_t datalen);
int vf_process_msg(msgctx_t *ctx, int nodeid, generic_msg_hdr *msgp, int nbytes);
int vf_end(char *keyid);
int vf_read(cluster_member_list_t *membership, char *keyid, uint64_t *view,
	    void **data, uint32_t *datalen);
	       
/* Reply to request for current data */
static int vf_send_current(msgctx_t *, char *);


struct vf_args {
	msgctx_t *ctx;
	int local_node_id;
	uint16_t port;
};


static int
_send_simple(msgctx_t *ctx, int32_t command, int arg1, int arg2, int log_errors)
{
	generic_msg_hdr hdr;

	hdr.gh_magic = GENERIC_HDR_MAGIC;
	hdr.gh_length = sizeof(hdr);
	hdr.gh_command = command;
	hdr.gh_arg1 = arg1;
	hdr.gh_arg2 = arg2;

	swab_generic_msg_hdr(&hdr);

	return msg_send(ctx, &hdr, sizeof(hdr));
}


static int 
vf_send_abort(msgctx_t *ctx, uint32_t trans)
{
#ifdef DEBUG
	printf("VF: Broadcasting ABORT (X#%08x)\n", trans);
#endif
	return _send_simple(ctx, VF_MESSAGE, VF_ABORT, trans, 0);
}


static int
vf_send_commit(msgctx_t *ctx, uint32_t trans)
{
#ifdef DEBUG
	printf("VF: Broadcasting FORMED\n");
#endif
	return _send_simple(ctx, VF_MESSAGE, VF_VIEW_FORMED, trans, 1);
}


static key_node_t *
kn_find_key(char *keyid)
{
	key_node_t *cur;

	for (cur = key_list; cur; cur = cur->kn_next)
		if (!strcmp(cur->kn_keyid,keyid))
			return cur;

	return NULL;
}


static key_node_t *
kn_find_trans(uint32_t trans)
{
	key_node_t *cur;
	view_node_t *curvn;

	for (cur = key_list; cur; cur = cur->kn_next)
		for (curvn = cur->kn_jvlist; curvn; curvn = curvn->vn_next)
			if (curvn->vn_transaction == trans)
				return cur;

	return NULL;
}


static int
vf_handle_join_view_msg(msgctx_t *ctx, int nodeid, vf_msg_t * hdrp)
{
	struct timeval timeout;
	key_node_t *key_node;
	uint32_t trans;

	trans = hdrp->vm_msg.vf_transaction;
#ifdef DEBUG
	printf("VF_JOIN_VIEW from member #%d! Key: %s #%d (X#%08x)\n",
	       hdrp->vm_msg.vf_coordinator, hdrp->vm_msg.vf_keyid,
	       (int) hdrp->vm_msg.vf_view, trans);
#endif

	pthread_mutex_lock(&key_list_mutex);
	key_node = kn_find_key(hdrp->vm_msg.vf_keyid);

	/*
	 * Call the voting callback function to see if we should continue.
	 */
	if (!key_node) {
		if ((vf_key_init_nt(hdrp->vm_msg.vf_keyid,
				    VF_COMMIT_TIMEOUT_MIN, NULL,
				    NULL) < 0)) {
			pthread_mutex_unlock(&key_list_mutex);
			printf("VF: Error: Failed to initialize %s\n",
			       hdrp->vm_msg.vf_keyid);
			vf_vote_no(ctx, trans);  
			return VFR_ERROR;
		}

		key_node = kn_find_key(hdrp->vm_msg.vf_keyid);
		assert(key_node);
	}

	if (key_node->kn_vote_cb) {
		if ((key_node->kn_vote_cb)(hdrp->vm_msg.vf_keyid,
					   hdrp->vm_msg.vf_view,
					   hdrp->vm_msg.vf_data,
					   hdrp->vm_msg.vf_datalen) == 0) {
			pthread_mutex_unlock(&key_list_mutex);
#ifdef DEBUG
			printf("VF: Voting NO (via callback)\n");
#endif
			vf_vote_no(ctx, trans);
			return VFR_OK;
		}
	}
	
	/*
	 * Buffer the join-view message.
	 */
	timeout.tv_sec = key_node->kn_tsec;
	timeout.tv_usec = 0;

	if (vf_buffer_join_msg((vf_msg_t *) hdrp, &timeout)) {
		pthread_mutex_unlock(&key_list_mutex);
#ifdef DEBUG
		printf("VF: Voting YES (X#%08x)\n", trans);
#endif
		vf_vote_yes(ctx, trans);
		return VFR_OK;
	}

	pthread_mutex_unlock(&key_list_mutex);
#ifdef DEBUG
	printf("VF: Voting NO\n");
#endif
	vf_vote_no(ctx, trans);
	return VFR_NO;
}


/*
 * Try to resolve (JOIN_VIEW, FORMED_VIEW) messages in the proper order.
 * Returns the number of commits.
 */
static int
vf_resolve_views(key_node_t *key_node)
{
	int commits = 0;
	void *data;
	uint32_t datalen;
	uint32_t trans;

	if (!key_node)
		return 0;

	while ((trans = vf_try_commit(key_node)) != 0) {
		commits++;
	}

	if (key_node->kn_commit_cb) {
		data = malloc(key_node->kn_datalen);
		if (!data) {
			/* XXX */
			return commits;
		}

		datalen = key_node->kn_datalen;
		memcpy(data, key_node->kn_data, datalen);

		(key_node->kn_commit_cb)(key_node->kn_keyid,
					 key_node->kn_viewno,
					 data,
					 datalen);
	}

	return commits;
}


static int
vf_unanimous(msgctx_t *mcast_ctx, int trans, int remain,
	     int timeout)
{
	generic_msg_hdr response;
	int x;

	/*
	 * Flag hosts which we received messages from so we don't
	 * read a second message.
	 */
	while (remain && timeout) {

		if (msg_wait(mcast_ctx, 1) <= 0) {
			--timeout;
			continue;
		}

		x = msg_receive(mcast_ctx, &response, sizeof(response), 1);
		if (x < sizeof(response))
			continue;
		
		/*
		 * Decode & validate message
		 */
		swab_generic_msg_hdr(&response);
		if ((response.gh_magic != GENERIC_HDR_MAGIC) ||
		    (response.gh_command != VF_MESSAGE)) {
			/* Don't process anything but votes */
			continue;
		}

		if (vf_command(response.gh_arg1) != VF_VOTE)
			/* Don't process anything but votes */
			continue;

		if (response.gh_arg2 != trans)
			continue;

		/*
		 * If we get a 'NO', we are done.
		 */
		if (!(vf_flags(response.gh_arg1) & VFMF_AFFIRM)) {
			/*
			 * XXX ok, it might be a mangled message;
			 * treat it as no anyway!
			 */
#ifdef DEBUG
			printf("VF: Abort: someone voted NO\n");
#endif
			return VFR_ABORT;
		}

#ifdef DEBUG
		printf("VF: YES\n");
#endif
		--remain;
	}

	if (remain) {
#ifdef DEBUG
		printf("VF: Timed out waiting for %d responses\n", remain);
#endif
		return VFR_TIMEOUT;
	}
		

	/*
	 * Whohoooooooo!
	 */
	return VFR_OK;
}


/*
 * ...
 */
static view_node_t *
vn_new(uint32_t trans, uint32_t nodeid, int viewno, void *data,
       uint32_t datalen)
{
	view_node_t *new;
	size_t totallen;

	totallen = sizeof(*new) + datalen;
	new = malloc(totallen);
	if (!new)
		return NULL;

	memset(new,0,totallen);

	new->vn_transaction = trans;
	new->vn_nodeid = nodeid;
	new->vn_viewno = viewno;
	new->vn_datalen = datalen;
	memcpy(new->vn_data, data, datalen);

	return new;
}


static int
vn_cmp(view_node_t *left, view_node_t *right)
{
	if ((left->vn_viewno < right->vn_viewno) || 
	    ((left->vn_viewno == right->vn_viewno) &&
	     (left->vn_nodeid < right->vn_nodeid)))
		return -1;

	/* Equal? ERROR!!! */
	if ((left->vn_viewno == right->vn_viewno) &&
	    (left->vn_nodeid == right->vn_nodeid))
		return 0;

	return 1;
}


static int
vn_insert_sorted(view_node_t **head, view_node_t *node)
{
	view_node_t *cur = *head, *back = NULL;

	/* only entry */
	if (!cur) {
		*head = node;
		return 1;
	}

	while (cur) {
		switch (vn_cmp(node, cur)) {
		case 0:
			/* duplicate */
			return 0;
		case -1:
			if (back) {
				/* middle */
				node->vn_next = cur;
				back->vn_next = node;
				return 1;
			}

			node->vn_next = *head;
			*head = node;
			return 1;
		}

		back = cur;
		cur = cur->vn_next;
	}

	/* end */
	back->vn_next = node;
	node->vn_next = NULL;

	return 1;
}


static view_node_t *
vn_remove(view_node_t **head, uint32_t trans)
{
	view_node_t *cur = *head, *back = NULL;

	if (!cur)
		return NULL;

	do {
		if (cur->vn_transaction == trans) {
			if (back) {
				back->vn_next = cur->vn_next;
				cur->vn_next = NULL;
				return cur;
			}

			*head = cur->vn_next;
			cur->vn_next = NULL;
			return cur;
		}

		back = cur;
		cur = cur->vn_next;
	} while (cur);

	return NULL;
}


/*
 * Buffer a join-view message.  We attempt to resolve the buffered join-view
 * messages whenever:
 * (a) we receive a commit message
 * (b) we don't receive any messages.
 */
static int
vf_buffer_join_msg(vf_msg_t *hdr, struct timeval *timeout)
{
	key_node_t *key_node;
	view_node_t *newp;
	int rv = 0;

	key_node = kn_find_key(hdr->vm_msg.vf_keyid);
	if (!key_node) {
		printf("Key %s not initialized\n",
		       hdr->vm_msg.vf_keyid);
		return 0;
	}

	/*
	 * Store if the view < viewno.
	 */
	if (hdr->vm_msg.vf_view < key_node->kn_viewno) {
		return 0;
	}

	newp = vn_new(hdr->vm_msg.vf_transaction, hdr->vm_msg.vf_coordinator,
		      hdr->vm_msg.vf_view, 
		      hdr->vm_msg.vf_data, hdr->vm_msg.vf_datalen);

	if (timeout && (timeout->tv_sec || timeout->tv_usec)) {
		if (getuptime(&newp->vn_timeout) == -1) {
			/* XXX What do we do here? */
			free(newp);
			return 0;
		}
	
		newp->vn_timeout.tv_sec += timeout->tv_sec;
		newp->vn_timeout.tv_usec += timeout->tv_usec;
	}

	rv = vn_insert_sorted(&key_node->kn_jvlist, newp);
	if (!rv)
		free(newp);

	return rv;
}


/*
 * XXX...
 */
static int
vc_cmp(commit_node_t *left, commit_node_t *right)
{
	if (left->vc_transaction < right->vc_transaction)
		return -1;

	if (left->vc_transaction == right->vc_transaction)
		return 0;

	return 1;
}


static int
vc_insert_sorted(commit_node_t **head, commit_node_t *node)
{
	commit_node_t *cur = *head, *back = NULL;

	/* only entry */
	if (!cur) {
		*head = node;
		return 1;
	}

	while (cur) {
		switch (vc_cmp(node, cur)) {
		case 0:
			/* duplicate */
			return 0;
		case -1:
			if (back) {
				/* middle */
				node->vc_next = cur;
				back->vc_next = node;
				return 1;
			}

			node->vc_next = *head;
			*head = node;
			return 1;
		}

		back = cur;
		cur = cur->vc_next;
	}

	/* end */
	back->vc_next = node;
	node->vc_next = NULL;

	return 1;
}


static commit_node_t *
vc_remove(commit_node_t **head, uint32_t trans)
{
	commit_node_t *cur = *head, *back = NULL;

	if (!cur)
		return NULL;

	do {
		if (cur->vc_transaction == trans) {
			if (back) {
				back->vc_next = cur->vc_next;
				cur->vc_next = NULL;
				return cur;
			}

			*head = cur->vc_next;
			cur->vc_next = NULL;
			return cur;
		}

		back = cur;
		cur = cur->vc_next;
	} while (cur);

	return NULL;
}


/*
 * Buffer a commit message received on a file descriptor.  We don't need
 * to know the node id; since the file descriptor will still be open from
 * the last 'join-view' message.
 */
static int
vf_buffer_commit(uint32_t trans)
{
	key_node_t *key_node;
	commit_node_t *newp;
	int rv;

	key_node = kn_find_trans(trans);
	if (!key_node)
		return 0;

	newp = malloc(sizeof(*newp));
	if (!newp)
		return 0;

	newp->vc_next = NULL;
	newp->vc_transaction = trans;

	rv = vc_insert_sorted(&key_node->kn_clist, newp);
	if (!rv)
		free(newp);

	return rv;
}


static int
vf_vote_yes(msgctx_t *ctx, uint32_t trans)
{
	/* XXX */
	return _send_simple(ctx, VF_MESSAGE, VF_VOTE | VFMF_AFFIRM, trans, 0);
}


static int
vf_vote_no(msgctx_t *ctx, uint32_t trans)
{
	/* XXX */
	return _send_simple(ctx, VF_MESSAGE, VF_VOTE, trans, 0);
}


static int
vf_abort(uint32_t trans)
{
	key_node_t *key_node;
	view_node_t *cur;

	key_node = kn_find_trans(trans);
	if (!key_node)
		return -1;

	cur = vn_remove(&key_node->kn_jvlist, trans);
	if (!cur)
		return -1;

	free(cur);
	return 0;
}


static int
tv_cmp(struct timeval *left, struct timeval *right)
{
	if (left->tv_sec > right->tv_sec)
		return 1;

	if (left->tv_sec < right->tv_sec)
		return -1;

	if (left->tv_usec > right->tv_usec)
		return 1;

	if (left->tv_usec < right->tv_usec)
		return -1;

	return 0;
}


/**
 * Grab the uptime from /proc/uptime.
 * 
 * @param tv		Timeval struct to store time in.  The sec
 * 			field contains seconds, the usec field 
 * 			contains the hundredths-of-seconds (converted
 * 			to micro-seconds)
 * @return		-1 on failure, 0 on success.
 */
int
getuptime(struct timeval *tv)
{
	FILE *fp;
	struct timeval junk;
	int rv;
	
	fp = fopen("/proc/uptime","r");
	
	if (!fp)
		return -1;

#if defined(__sparc__)
	rv = fscanf(fp,"%ld.%d %ld.%d\n", &tv->tv_sec, &tv->tv_usec,
		    &junk.tv_sec, &junk.tv_usec);
#else
	rv = fscanf(fp,"%ld.%ld %ld.%ld\n", &tv->tv_sec, &tv->tv_usec,
		    &junk.tv_sec, &junk.tv_usec);
#endif
	fclose(fp);
	
	if (rv != 4) {
		return -1;
	}
	
	tv->tv_usec *= 10000;
	
	return 0;
}


/**
 * Try to commit views in a given key_node.
 */
static uint32_t
vf_try_commit(key_node_t *key_node)
{
	view_node_t *vnp;
	commit_node_t *cmp;
	uint32_t trans = 0;

	if (!key_node)
		return 0;

	if (!key_node->kn_jvlist)
		return 0;

	trans = key_node->kn_jvlist->vn_transaction;
		
	cmp = vc_remove(&key_node->kn_clist, trans);
	if (!cmp) {
		/*printf("VF: Commit for fd%d not received yet!", fd);*/
		return 0;
	}

	free(cmp); /* no need for it any longer */
		
	vnp = vn_remove(&key_node->kn_jvlist, trans);
	if (!vnp) {
		/*
		 * But, we know it was the first element on the list?!!
		 */
		fprintf(stderr,"VF: QUAAAAAAAAAAAAAACKKKK!");
		raise(SIGSTOP);
	}
	
#ifdef DEBUG
	printf("VF: Commit Key %s #%d from member #%d\n",
	       key_node->kn_keyid, (int)vnp->vn_viewno, vnp->vn_nodeid);
#endif

	/*
	 * Store the current view of everything in our key node
	 */
	key_node->kn_viewno = vnp->vn_viewno;
	if (key_node->kn_data)
		free(key_node->kn_data);
	key_node->kn_datalen = vnp->vn_datalen;
	key_node->kn_data = malloc(vnp->vn_datalen);

	/*
	 *   Need to check return of malloc always
	 */
	if (key_node->kn_data == NULL) {
		fprintf (stderr, "malloc fail err=%d\n", errno);
		return -1;
	}

	memcpy(key_node->kn_data, vnp->vn_data, vnp->vn_datalen);

	free(vnp);
	return trans;
}


void
vf_event_loop(msgctx_t *ctx, int my_node_id)
{
	int n;
	generic_msg_hdr *hdrp = NULL;

	if (msg_wait(ctx, 3) != 0) {

		n = msg_receive_simple(ctx, &hdrp, 2);

		if (n <= 0 || !hdrp) {
			return;
		}

		swab_generic_msg_hdr(hdrp);
		if (hdrp->gh_command == VF_MESSAGE &&
		    hdrp->gh_arg1 != VF_CURRENT) {
			if (vf_process_msg(ctx, 0, hdrp, n) == VFR_COMMIT) {
#ifdef DEBUG
				printf("VFT: View committed\n");
#endif
			}
		}

		if (hdrp) {
			free(hdrp);
			hdrp = NULL;
		}
	}
}


static void
vf_wait_ready(void)
{
	pthread_mutex_lock(&vf_mutex);
	while (!vf_thread_ready) {
		pthread_mutex_unlock(&vf_mutex);
		usleep(50000);
		pthread_mutex_lock(&vf_mutex);
	}
	pthread_mutex_unlock(&vf_mutex);
}


void *
vf_server(void *arg)
{
	int my_node_id;
	uint16_t port;
	key_node_t *cur;
	uint32_t trans;
	msgctx_t *ctx;

	block_all_signals();

	port = ((struct vf_args *)arg)->port;
	my_node_id = ((struct vf_args *)arg)->local_node_id;
	ctx = ((struct vf_args *)arg)->ctx;
	free(arg);

#ifdef DEBUG
	printf("VFT: Thread id %ld starting\n", (long)pthread_self());
#endif

	pthread_mutex_lock(&vf_mutex);
	vf_thread_ready = 1;
	pthread_mutex_unlock(&vf_mutex);

	while (vf_thread_ready) {
		pthread_mutex_lock(&key_list_mutex);
		for (cur = key_list; cur; cur = cur->kn_next) {
			/* Destroy timed-out join views */
			while (_vf_purge(cur, &trans) != VFR_NO);
		}
		pthread_mutex_unlock(&key_list_mutex);
		vf_event_loop(ctx, my_node_id);
	}

	msg_close(ctx);
	msg_free_ctx(ctx);
	pthread_exit(NULL);
}



/**
 * Initialize VF.  Initializes the View Formation sub system.
 * @param my_node_id	The node ID of the caller.
 * @return		0 on success, -1 on failure.
 */
int
vf_init(int my_node_id, uint16_t my_port, vf_vote_cb_t vcb,
	vf_commit_cb_t ccb)
{
	struct vf_args *args;
	msgctx_t *ctx;
	if (my_node_id == (int)-1)
		return -1;
	
	while((ctx = msg_new_ctx()) == NULL)
		sleep(1);

	while((args = malloc(sizeof(*args))) == NULL)
		sleep(1);

	if (msg_open(MSG_CLUSTER, 0, my_port, ctx, 1) < 0) {
		msg_free_ctx(ctx);	
		free(args);
		return -1;
	}

	args->port = my_port;
	args->local_node_id = my_node_id;
	args->ctx = ctx;


	pthread_mutex_lock(&vf_mutex);
	_port = my_port;
	_node_id = my_node_id;
	default_vote_cb = vcb;
	default_commit_cb = ccb;
	pthread_mutex_unlock(&vf_mutex);

	pthread_create(&vf_thread, NULL, vf_server, args);

	vf_wait_ready();

	return 0;
}


int
vf_invalidate(void)
{
	key_node_t *c_key;
	view_node_t *c_jv;
	commit_node_t *c_cn;

	pthread_mutex_lock(&key_list_mutex);

	while ((c_key = key_list) != NULL) {

		while ((c_jv = c_key->kn_jvlist) != NULL) {
			key_list->kn_jvlist = c_jv->vn_next;
			free(c_jv);
		}

		while ((c_cn = c_key->kn_clist) != NULL) {
			c_key->kn_clist = c_cn->vc_next;
			free(c_cn);
		}

		key_list = c_key->kn_next;

		if (c_key->kn_data)
			free(c_key->kn_data);
		free(c_key->kn_keyid);
		free(c_key);
	}

	pthread_mutex_unlock(&key_list_mutex);
	return 0;
}


/**
  Shut down VF
  */
int
vf_shutdown(void)
{
	pthread_mutex_lock(&vf_mutex);
	vf_thread_ready = 0;
	pthread_cancel(vf_thread);
	pthread_join(vf_thread, NULL);
	_port = 0;
	_node_id = (int)-1;

	vf_invalidate();

	pthread_mutex_unlock(&vf_mutex);

	return 0;
}


/**
 * Adds a key to key node list and sets up callback functions.
 *
 * @param keyid		The ID of the key to add.
 * @param timeout	Amount of time to wait before purging a JOIN_VIEW
 *			message from our buffers.
 * @param vote_cb	Function to call on a given data set/view number
 *			to help decide whether to vote yes or no.  This is
 *			optional, and DOES NOT obviate the need for VF's
 *			decision-making (version/node ID based).  Also,
 *			the data from the view-node is passed UNCOPIED to
 *			the callback function!
 * @param commit_cb	Function to call when a key has had one or more
 *			commits.  Same info applies: the data passed to the
 *			callback function is UNCOPIED.
 * @return 0 (always)
 */
static int
vf_key_init_nt(char *keyid, int timeout, vf_vote_cb_t vote_cb,
   	       vf_commit_cb_t commit_cb)
{
	key_node_t *newnode = NULL;
	
	newnode = kn_find_key(keyid);
	if (newnode) {
		printf("Key %s already initialized\n", keyid);
		pthread_mutex_unlock(&key_list_mutex);
		return -1;
	}

	newnode = malloc(sizeof(*newnode));

	if (newnode == NULL) {
		fprintf(stderr, "malloc fail3 err=%d\n", errno);
		pthread_mutex_unlock(&key_list_mutex);
		return -1;
	}

	newnode->kn_data = NULL;
	memset(newnode,0,sizeof(*newnode));
	newnode->kn_keyid = strdup(keyid);

	/* Set up callbacks */
	if (vote_cb)
		newnode->kn_vote_cb = vote_cb;
	else
		newnode->kn_vote_cb = default_vote_cb;

	if (commit_cb) 
		newnode->kn_commit_cb = commit_cb;
	else
		newnode->kn_commit_cb = default_commit_cb;

	if (timeout < VF_COMMIT_TIMEOUT_MIN) {
		/* Join View message timeout must exceed the
		   coordinator timeout */
		timeout = VF_COMMIT_TIMEOUT_MIN;
	}
	newnode->kn_tsec = timeout;

	newnode->kn_next = key_list;
	key_list = newnode;

	return 0;
}


int
vf_key_init(char *keyid, int timeout, vf_vote_cb_t vote_cb,
	    vf_commit_cb_t commit_cb)
{
	int rv;

	pthread_mutex_lock(&key_list_mutex);
	rv = vf_key_init_nt(keyid, timeout, vote_cb, commit_cb);
	pthread_mutex_unlock(&key_list_mutex);

	return 0;
}


vf_msg_t *
build_vf_data_message(int cmd, char *keyid, void *data, uint32_t datalen,
		      int viewno, int trans, uint32_t *retlen)
{
	uint32_t totallen;
	vf_msg_t *msg;
	/*
	 * build the message
	 */
	totallen = sizeof(vf_msg_t) + datalen;
	msg = malloc(totallen);
	*retlen = 0;
	if (!msg)
		return NULL;
	memset(msg, 0, totallen);
	
	/* header */
	msg->vm_hdr.gh_magic = GENERIC_HDR_MAGIC;
	msg->vm_hdr.gh_length = totallen;
	msg->vm_hdr.gh_command = VF_MESSAGE;
	msg->vm_hdr.gh_arg1 = cmd;

	/* Data */
	strncpy(msg->vm_msg.vf_keyid,keyid,sizeof(msg->vm_msg.vf_keyid));
	msg->vm_msg.vf_transaction = trans;
	msg->vm_msg.vf_datalen = datalen;
	msg->vm_msg.vf_coordinator = _node_id;
	msg->vm_msg.vf_view = viewno;
	memcpy(msg->vm_msg.vf_data, data, datalen);

	*retlen = totallen;
	return msg;
}


/**
 * Begin VF.  Begins View-Formation for agiven set of data.
 *
 * @param membership	Current membership.
 * @param flags		Operational flags.
 * @param keyid		Key ID of the data to distribute.
 * @param data		The actual data to distribute.
 * @param datalen	The length of the data.
 * @param viewno	The current view number of the data.
 * @param block		Block until completion?
 * @return		-1 on failure, or 0 on success.  The parent will
 * 			either get a SIGCHLD or can randomly call vf_end()
 * 			on keyid to cause the VF child to be cleaned up.
 * @see vf_end
 */
int
vf_write(cluster_member_list_t *membership, uint32_t flags, char *keyid,
	 void *data, uint32_t datalen)
{
	msgctx_t everyone;
	key_node_t *key_node;
	vf_msg_t *join_view;
	int remain = 0, x, y, rv = VFR_ERROR;
	uint32_t totallen;
#ifdef DEBUG
	struct timeval start, end, dif;
#endif
	struct dlm_lksb lockp;
	int l;
	char lock_name[256];
	static uint32_t trans = 0;

	if (!data || !datalen || !keyid || !strlen(keyid) || !membership)
		return -1;

	pthread_mutex_lock(&vf_mutex);
	if (!trans) {
		trans = _node_id << 16;
	}
	++trans;

	/* Obtain cluster lock on it. */
	snprintf(lock_name, sizeof(lock_name), "usrm::vf");
	l = clu_lock(LKM_EXMODE, &lockp, 0, lock_name);
	if (l < 0) {
		pthread_mutex_unlock(&vf_mutex);
		return l;
	}

#ifdef DEBUG
	getuptime(&start);
#endif

	remain = 0;
	for (x = 0, y = 0; x < membership->cml_count; x++) {
		if (membership->cml_members[x].cn_member) {
			remain++;
		}
	}

#ifdef DEBUG
	printf("Allright, need responses from %d members\n", remain);
#endif

	pthread_mutex_lock(&key_list_mutex);
	key_node = kn_find_key(keyid);
	if (!key_node) {

		if ((vf_key_init_nt(keyid, 10, NULL, NULL) < 0)) {
			pthread_mutex_unlock(&key_list_mutex);
			clu_unlock(&lockp);
			pthread_mutex_unlock(&vf_mutex);
			return -1;
		}
		key_node = kn_find_key(keyid);
		assert(key_node);
	}

	join_view = build_vf_data_message(VF_JOIN_VIEW, keyid, data, datalen,
					  key_node->kn_viewno+1, trans, &totallen);

	pthread_mutex_unlock(&key_list_mutex);

	if (!join_view) {
		clu_unlock(&lockp);
		pthread_mutex_unlock(&vf_mutex);
		return -1;
	}

#ifdef DEBUG
	printf("VF: Push %d.%d #%d (X#%08x)\n", (int)_node_id, getpid(),
	       (int)join_view->vm_msg.vf_view, trans);
#endif
	/* 
	 * Encode the package.
	 */
	swab_vf_msg_t(join_view);

	/*
	 * Send our message to everyone
	 */
	if (msg_open(MSG_CLUSTER, 0, _port, &everyone, 0) < 0) {
		printf("msg_open: fail: %s\n", strerror(errno));
		clu_unlock(&lockp);
		pthread_mutex_unlock(&vf_mutex);
		return -1;
	}

	x = msg_send(&everyone, join_view, totallen);
	if (x < totallen) {
		vf_send_abort(&everyone, trans);
#ifdef DEBUG
		printf("VF: Aborted: Send failed (%d/%d)\n", x, totallen);
#endif
		msg_close(&everyone);
		free(join_view);
		clu_unlock(&lockp);
		pthread_mutex_unlock(&vf_mutex);
		return -1;
	} 

#ifdef DEBUG
	printf("VF: Checking for consensus...\n");
#endif
	/*
	 * See if we have a consensus =)
	 */
	if ((rv = (vf_unanimous(&everyone, trans, remain,
				5))) == VFR_OK) {
		vf_send_commit(&everyone, trans);
#ifdef DEBUG
		printf("VF: Consensus reached!\n");
#endif
	} else {
		vf_send_abort(&everyone, trans);
#ifdef DEBUG
		printf("VF: Aborted!\n");
#endif
	}

	/*
	 * unanimous returns 1 for true; 0 for false, so negate it and
	 * return our value...
	 */
	msg_close(&everyone);
	free(join_view);
	clu_unlock(&lockp);
	pthread_mutex_unlock(&vf_mutex);

#ifdef DEBUG
	if (rv == VFR_OK) {
		getuptime(&end);

		dif.tv_usec = end.tv_usec - start.tv_usec;
		dif.tv_sec = end.tv_sec - start.tv_sec;
		
		if (dif.tv_usec < 0) {
		    dif.tv_usec += 1000000;
		    dif.tv_sec--;
		}

		printf("VF: Converge Time: %d.%06d\n", (int)dif.tv_sec,
		       (int)dif.tv_usec);
	}
#endif

	return rv;
}


/**
 * Purge an unresolved JOIN-VIEW message if it has expired.  This only
 * purges a single message; if used, it should be called in a while()
 * loop.  The function returns a file descriptor which can be closed and
 * cleaned up by the caller if a request has indeed timed out.  Also,
 * if a request has timed out, the function calls vf_resolve_views to try
 * to resolve any outstanding views which were opened up by the timed-out
 * request.
 *
 * @param keyid		Key ID on which to purge timeouts.
 * @param fd		Pointer which, upon return, will either contain -1
 *			whenever VFR_NO is the return value, or the file
 *			descriptor which was resolved.
 * @return		VFR_ERROR on error.  VFR_NO if there are no timed-out
 *			requests, or if there are no requests at all, or if
 *			keyid isn't valid.  VFR_OK if there are timed-out
 *			requests and the virtue of removing the timed-out
 *			requests did not cause commit-resolution, or
 *			VFR_COMMIT if new views	were committed.  
 */
static int
_vf_purge(key_node_t *key_node, uint32_t *trans)
{
	view_node_t *cur, *dead = NULL;
	struct timeval tv;

	*trans = 0;
	
	if (!key_node)
		return VFR_NO;

	cur = key_node->kn_jvlist;
	if (!cur)
		return VFR_NO;

	if (getuptime(&tv) == -1) {
		fprintf(stderr,"VF: getuptime(): %s\n", strerror(errno));
		return VFR_ERROR;
	}

	for (; cur; cur = cur->vn_next) {
		if (tv_cmp(&tv, &cur->vn_timeout) < 0)
			continue;

		*trans = cur->vn_transaction;
		dead = vn_remove(&key_node->kn_jvlist, *trans);
		free(dead);

		printf("VF: Killed transaction %08x\n", *trans);
		/*
		 * returns the removed associated file descriptor
		 * so that we can close it and get on with life
		 */
		break;
	}

	if (*trans == 0)
		return VFR_NO;
		
	if (vf_resolve_views(key_node))
		return VFR_COMMIT;
	return VFR_OK;
}


/**
 * Process a VF message.
 *
 * @param nodeid	Node id from which msgp was received.
 * @param msgp		Pointer to already-received message.
 * @param nbytes	Length of msgp.
 * @return		-1 on failure, 0 on success.
 */
int
vf_process_msg(msgctx_t *ctx, int nodeid, generic_msg_hdr *msgp, int nbytes)
{
	vf_msg_t *hdrp;
	int ret;
	key_node_t *kn;

	if ((nbytes <= 0) || (nbytes < sizeof(generic_msg_hdr)) ||
	    (msgp->gh_command != VF_MESSAGE))
		return VFR_ERROR;

	switch(vf_command(msgp->gh_arg1)) {
	case VF_CURRENT:
#ifdef DEBUG
		printf("VF: Received request for current data\n");
#endif
		
		/* Validate size... */
		if (nbytes < sizeof(*hdrp)) {
			fprintf(stderr, "VF: JOIN_VIEW message too short!\n");
			return VFR_ERROR;
		}

		hdrp = (vf_msg_t *)msgp;
		swab_vf_msg_info_t(&hdrp->vm_msg);

		return vf_send_current(ctx, hdrp->vm_msg.vf_keyid);
	
	case VF_JOIN_VIEW:
		/* Validate size... */
		if (nbytes < sizeof(*hdrp)) {
			fprintf(stderr, "VF: JOIN_VIEW message too short!\n");
			return VFR_ERROR;
		}

		/* Unswap so we can swab the whole message */
		hdrp = (vf_msg_t *)msgp;
		swab_vf_msg_info_t(&hdrp->vm_msg);

		if ((hdrp->vm_msg.vf_datalen + sizeof(*hdrp)) != nbytes) {
			fprintf(stderr, "VF: JOIN_VIEW: Invalid size %d/%d\n",
				nbytes, hdrp->vm_msg.vf_datalen +
				(uint32_t)sizeof(*hdrp));

			return VFR_ERROR;
		}
		return vf_handle_join_view_msg(ctx, nodeid, hdrp);
		
	case VF_ABORT:
		printf("VF: Received VF_ABORT (X#%08x)\n", msgp->gh_arg2);
		vf_abort(msgp->gh_arg2);
		return VFR_ABORT;
		
	case VF_VIEW_FORMED:
#ifdef DEBUG
		printf("VF: Received VF_VIEW_FORMED, %d\n",
		       nodeid);
#endif
		pthread_mutex_lock(&key_list_mutex);
		vf_buffer_commit(msgp->gh_arg2);
		kn = kn_find_trans(msgp->gh_arg2);
		if (!kn) {
			pthread_mutex_unlock(&key_list_mutex);
			return VFR_OK;
		}

		ret = (vf_resolve_views(kn) ? VFR_COMMIT : VFR_OK);
		pthread_mutex_unlock(&key_list_mutex);
		return ret;

	default:
		/* Ignore votes and the like from this part */
		break;
	}

	return VFR_OK;
}


/**
 * Retrieves the current dataset for a given key ID.
 *
 * @param keyid		Key ID of data set to retrieve.
 * @param view		Pointer which will be filled with the current data
 *			set's view number.
 * @param data		Pointer-to-pointer which will be allocated and
 *			filled with the current data set.  Caller must free.
 * @param datalen	Pointer which will be filled with the current data
 *			set's size.
 * @return		-1 on failure, 0 on success.
 */
int
vf_read(cluster_member_list_t *membership, char *keyid, uint64_t *view,
	void **data, uint32_t *datalen)
{
	key_node_t *key_node;
	char lock_name[256];
	struct dlm_lksb lockp;
	int l;

	/* Obtain cluster lock on it. */
	pthread_mutex_lock(&vf_mutex);
	snprintf(lock_name, sizeof(lock_name), "usrm::vf");
	l = clu_lock(LKM_EXMODE, &lockp, 0, lock_name);
	if (l < 0) {
		pthread_mutex_unlock(&vf_mutex);
		return l;
	}

	do {
		pthread_mutex_lock(&key_list_mutex);

		key_node = kn_find_key(keyid);
		if (!key_node) {
			if ((vf_key_init_nt(keyid, 10, NULL, NULL) < 0)) {
				pthread_mutex_unlock(&key_list_mutex);
				clu_unlock(&lockp);
				pthread_mutex_unlock(&vf_mutex);
				printf("Couldn't locate %s\n", keyid);
				return VFR_ERROR;
			}
			
			key_node = kn_find_key(keyid);
			assert(key_node);
		}

		/* XXX Don't allow reads during commits. */
		if (key_node->kn_jvlist || key_node->kn_clist)  {
			pthread_mutex_unlock(&key_list_mutex);
			usleep(10000);
			continue;
		}
	} while (0);

	if (!key_node->kn_data || !key_node->kn_datalen) {
		pthread_mutex_unlock(&key_list_mutex);

		if (!membership) {
			clu_unlock(&lockp);
			//printf("Membership NULL, can't find %s\n", keyid);
			pthread_mutex_unlock(&vf_mutex);
			return VFR_ERROR;
		}

		l = vf_request_current(membership, keyid, view, data,
				       datalen);
	       	if (l == VFR_NODATA || l == VFR_ERROR) {
			clu_unlock(&lockp);
			//printf("Requesting current failed %s %d\n", keyid, l);
			pthread_mutex_unlock(&vf_mutex);
			return l;
		}
	}

	*data = malloc(key_node->kn_datalen);
	if (! *data) {
		pthread_mutex_unlock(&key_list_mutex);
		clu_unlock(&lockp);
		pthread_mutex_unlock(&vf_mutex);
		printf("Couldn't malloc %s\n", keyid);
		return VFR_ERROR;
	}

	memcpy(*data, key_node->kn_data, key_node->kn_datalen);
	*datalen = key_node->kn_datalen;
	*view = key_node->kn_viewno;

	pthread_mutex_unlock(&key_list_mutex);
	clu_unlock(&lockp);
	pthread_mutex_unlock(&vf_mutex);

	return VFR_OK;
}


int
vf_read_local(char *keyid, int *view, void **data, uint32_t *datalen)
{
	key_node_t *key_node = NULL;

	pthread_mutex_lock(&vf_mutex);
	pthread_mutex_lock(&key_list_mutex);

	key_node = kn_find_key(keyid);
	if (!key_node) {
		pthread_mutex_unlock(&key_list_mutex);
		pthread_mutex_unlock(&vf_mutex);
		printf("no key for %s\n", keyid);
		return VFR_NODATA;
	}

	if (!key_node->kn_data || !key_node->kn_datalen) {
		pthread_mutex_unlock(&key_list_mutex);
		pthread_mutex_unlock(&vf_mutex);
		return VFR_NODATA;
	}

	*data = malloc(key_node->kn_datalen);
	if (! *data) {
		pthread_mutex_unlock(&key_list_mutex);
		pthread_mutex_unlock(&vf_mutex);
		printf("Couldn't malloc %s\n", keyid);
		return VFR_ERROR;
	}

	memcpy(*data, key_node->kn_data, key_node->kn_datalen);
	*datalen = key_node->kn_datalen;
	*view = key_node->kn_viewno;

	pthread_mutex_unlock(&key_list_mutex);
	pthread_mutex_unlock(&vf_mutex);

	return VFR_OK;
}


static int
vf_send_current(msgctx_t *ctx, char *keyid)
{
	key_node_t *key_node;
	vf_msg_t *msg;
	int ret;
	uint32_t totallen;

	if (!ctx || ctx->type == -1)
		return VFR_ERROR;

	pthread_mutex_lock(&key_list_mutex);

	key_node = kn_find_key(keyid);
	if (!key_node || !key_node->kn_data || !key_node->kn_datalen) {
		pthread_mutex_unlock(&key_list_mutex);
		printf("VFT: No data for keyid %s\n", keyid);
		return (_send_simple(ctx, VF_NACK, 0, 0, 0) != -1)?
			VFR_OK : VFR_ERROR;
	}

	/*
	 * XXX check for presence of nodes on the commit lists; send
	 * VF_AGAIN if there is any.
	 */
	msg = build_vf_data_message(VF_ACK, keyid, key_node->kn_data,
				    key_node->kn_datalen,
				    key_node->kn_viewno,
				    0,
				    &totallen);

	pthread_mutex_unlock(&key_list_mutex);
	if (!msg)
		return (_send_simple(ctx, VFR_ERROR, 0, 0, 0) != -1)?
			VFR_OK : VFR_ERROR;

	swab_vf_msg_t(msg);
	ret = (msg_send(ctx, msg, totallen) >= 0)?VFR_OK:VFR_ERROR;
	free(msg);
	return ret;
}


static int
vf_set_current(char *keyid, int view, void *data, uint32_t datalen)
{
	key_node_t *key_node;
	void *datatmp;

	pthread_mutex_lock(&key_list_mutex);

	key_node = kn_find_key(keyid);
	if (!key_node) {
		pthread_mutex_unlock(&key_list_mutex);
		return VFR_ERROR;
	}

	datatmp = malloc(datalen);
	if (! datatmp) {
		pthread_mutex_unlock(&key_list_mutex);
		return VFR_ERROR;
	}
	
	if (key_node->kn_data) {
		free(key_node->kn_data);
		key_node->kn_data = NULL;
	}

	key_node->kn_data = datatmp;
	memcpy(key_node->kn_data, data, datalen);
	key_node->kn_datalen = datalen;
	key_node->kn_viewno = view;

	pthread_mutex_unlock(&key_list_mutex);

	return VFR_OK;
}


/**
 * Request the current state of a keyid from the membership.
 * XXX This doesn't wait for outstanding transactions to complete.
 * Perhaps it should.
 *
 * @param membership	Membership mask.
 * @param keyid		VF key id (application-defined).
 * @param viewno	Return view number.  Passed in pre-allocated.
 * @param data		Return data pointer.  Allocated within.
 * @param datalen	Size of data returned.
 */
static int
vf_request_current(cluster_member_list_t *membership, char *keyid,
		   uint64_t *viewno, void **data, uint32_t *datalen)
{
	int x, n, rv = VFR_OK, port;
	msgctx_t ctx;
	vf_msg_t rmsg;
	vf_msg_t *msg = &rmsg;
	generic_msg_hdr * gh;
	int me;

	if (_port == 0) {
		return -1;
	}

	port = _port;
	me = _node_id;

	memset(msg, 0, sizeof(*msg));
	msg->vm_hdr.gh_magic = GENERIC_HDR_MAGIC;
	msg->vm_hdr.gh_length = sizeof(*msg);
	msg->vm_hdr.gh_command = VF_MESSAGE;
	msg->vm_hdr.gh_arg1 = VF_CURRENT;
	strncpy(msg->vm_msg.vf_keyid, keyid,
		sizeof(msg->vm_msg.vf_keyid));

	swab_generic_msg_hdr(&(msg->vm_hdr));
	swab_vf_msg_info_t(&(msg->vm_msg));

	for (x = 0; x < membership->cml_count; x++) {
		if (!membership->cml_members[x].cn_member)
			continue;

		/* Can't request from self. */
		if (membership->cml_members[x].cn_nodeid == me)
			continue;

		rv = VFR_ERROR;
		if (msg_open(MSG_CLUSTER,
			     membership->cml_members[x].cn_nodeid,
			     port, &ctx, 15) < 0) {
			continue;
		}

		msg = &rmsg;
		//printf("VF: Requesting current value of %s from %d\n",
		       //msg->vm_msg.vf_keyid,
		       //(int)membership->cml_members[x].cn_nodeid);

		if (msg_send(&ctx, msg, sizeof(*msg)) < sizeof(*msg)) {
			printf("Couldn't send entire message\n");
			msg_close(&ctx);
			continue;
		}

		gh = NULL;
		if ((n = msg_receive_simple(&ctx, (generic_msg_hdr **)&gh, 10))
		    < 0) {
			if (gh)
				free(gh);
			msg_close(&ctx);
			continue;
		}
		msg_close(&ctx);
		msg = (vf_msg_t *)gh;

		/* Uh oh */
		if (!msg || (msg == &rmsg)) {
			printf("VF: No valid message\n");
			return VFR_ERROR;
		}
		swab_generic_msg_hdr(&(msg->vm_hdr));
		if (msg->vm_hdr.gh_command == VF_NACK) {
			free(msg);
			continue;
		}
		if (msg->vm_hdr.gh_length < sizeof(vf_msg_t)) {
			fprintf(stderr, "VF: Short reply from %d\n", x);
			free(msg);
			continue;
		}
		if (msg->vm_hdr.gh_length > n) {
			fprintf(stderr,
				"VF: Size mismatch during decode (%d > %d)\n",
				msg->vm_hdr.gh_length, n);
			free(msg);
			continue;
		}

		swab_vf_msg_info_t(&(msg->vm_msg));

		if (msg->vm_msg.vf_datalen != (n - sizeof(*msg))) {
			fprintf(stderr,"VF: Size mismatch during decode (\n");
			free(msg);
			continue;
		}

		/* Ok... we've got data! */
		if (vf_set_current(keyid, msg->vm_msg.vf_view,
			   msg->vm_msg.vf_data,
			   msg->vm_msg.vf_datalen) == VFR_ERROR) {
			free(msg);
			return VFR_ERROR;
		}

		free(msg);

		return VFR_OK;
	}

	return VFR_NODATA;
}


void
dump_vf_states(FILE *fp)
{
	key_node_t *cur;

	fprintf(fp, "View-Formation States:\n");
	fprintf(fp, "  Thread: %d\n", (unsigned)vf_thread);
	fprintf(fp, "  Default callbacks:\n    Vote: %p\n    Commit: %p\n",
		default_vote_cb, default_commit_cb);
	fprintf(fp, "  Distributed key metadata:\n");

	pthread_mutex_lock(&key_list_mutex);

	for (cur = key_list; cur; cur = cur->kn_next) {
		fprintf(fp, "    %s, View: %d, Size: %d, Address: %p\n",
			cur->kn_keyid,
			(int)cur->kn_viewno,
			cur->kn_datalen,
			cur->kn_data);
		if (cur->kn_vote_cb != default_vote_cb) 
			fprintf(fp, "      Vote callback: %p\n", cur->kn_vote_cb);
		if (cur->kn_commit_cb != default_commit_cb) 
			fprintf(fp, "      Commit callback: %p\n", cur->kn_commit_cb);

		if (cur->kn_jvlist)
			fprintf(fp, "        This key has unresolved "
			        "new views pending\n");
 		if (cur->kn_clist)
			fprintf(fp, "        This key has unresolved "
			        "commits pending\n");

	}

	pthread_mutex_unlock(&key_list_mutex);
	fprintf(fp, "\n");
}
