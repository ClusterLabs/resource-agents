/*
  Copyright Red Hat, Inc. 2002-2003

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
#include <magma.h>
#include <magmamsg.h>
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


static int vf_lfds[2];
static int vf_lfd = 0;
static key_node_t *key_list = NULL;	/** List of key nodes. */
static uint64_t _node_id = (uint64_t)-1;/** Our node ID, set with vf_init. */
static uint16_t _port = 0;		/** Our daemon ID, set with vf_init. */

/*
 * TODO: We could make it thread safe, but this might be unnecessary work
 * Solution: Super-coarse-grained-bad-code-locking!
 */
static pthread_mutex_t key_list_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t vf_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t vf_thread = (pthread_t)-1;
static int thread_ready = 0;
static vf_vote_cb_t default_vote_cb = NULL;
static vf_vote_cb_t default_commit_cb = NULL;


/*
 * Internal Functions
 */
static int send_to_all(int *peer_fds, int32_t command, int arg1, int arg2,
		       int log_errors);
static int vf_send_abort(int *fds);
static int vf_send_commit(int *fds);
static void close_all(int *fds);
static key_node_t * kn_find_key(char *keyid);
static key_node_t * kn_find_fd(uint32_t fd);
static int vf_handle_join_view_msg(int fd, vf_msg_t * hdrp);
static int vf_resolve_views(key_node_t *key_node);
static int vf_unanimous(int *peer_fds, int remain, int timeout);
static view_node_t * vn_new(int fd, uint32_t nodeid, uint64_t viewno,
			    void *data, uint32_t datalen);
static int vf_request_current(cluster_member_list_t *membership, char *keyid,
		   	      uint64_t *viewno, void **data, uint32_t *datalen);
static int _vf_purge(key_node_t *key_node, int *fd);

/* Join-view buffer list functions */
static int vn_cmp(view_node_t *left, view_node_t *right);
static int vn_insert_sorted(view_node_t **head, view_node_t *node);
static view_node_t * vn_remove(view_node_t **head, int fd);
static int vf_buffer_join_msg(int fd, vf_msg_t *hdr,
			      struct timeval *timeout);

/* Commits buffer list functions */
static int vc_cmp(commit_node_t *left, commit_node_t *right);
static int vc_insert_sorted(commit_node_t **head, commit_node_t *node);
static commit_node_t * vc_remove(commit_node_t **head, int fd);
static int vf_buffer_commit(int fd);

/* Simple functions which client calls to vote/abort */
static int vf_vote_yes(int fd);
static int vf_vote_no(int fd);
static int vf_abort(int fd);
static int tv_cmp(struct timeval *left, struct timeval *right);

/* Resolution */
static int vf_try_commit(key_node_t *key_node);

int vf_init(uint64_t my_node_id, uint16_t my_port,
	    vf_vote_cb_t vote_cb, vf_commit_cb_t commit_cb);
int vf_key_init(char *keyid, int timeout, vf_vote_cb_t vote_cb,
		vf_commit_cb_t commit_cb);
static int vf_key_init_nt(char *keyid, int timeout, vf_vote_cb_t vote_cb,
			  vf_commit_cb_t commit_cb);
int vf_write(cluster_member_list_t *memberhip, uint32_t flags,
	     char *keyid, void *data, uint32_t datalen);
int vf_process_msg(int handle, generic_msg_hdr *msgp, int nbytes);
int vf_end(char *keyid);
int vf_read(cluster_member_list_t *membership, char *keyid, uint64_t *view,
	    void **data, uint32_t *datalen);
	       
/* Reply to request for current data */
static int vf_send_current(int fd, char *);


struct vf_args {
	uint16_t port;
	uint64_t local_node_id;
};


static int
send_to_all(int *peer_fds, int32_t command, int arg1, int arg2, int log_errors)
{
	generic_msg_hdr hdr;
	int x, rv = 0;

	hdr.gh_magic = GENERIC_HDR_MAGIC;
	hdr.gh_length = sizeof(hdr);
	hdr.gh_command = command;
	hdr.gh_arg1 = arg1;
	hdr.gh_arg2 = arg2;

	swab_generic_msg_hdr(&hdr);

	for (x=0; peer_fds[x] != -1; x++) {
		if (msg_send(peer_fds[x], &hdr, sizeof(hdr)) == sizeof(hdr))
			continue;

		if (log_errors) {
#if 0
			clulog(LOG_ERR, "#14: Failed to send %d "
			       "bytes to %d!\n", sizeof(hdr),
			       x);
#endif
		}
		rv = -1;
	}

	return rv;
}


static int 
vf_send_abort(int *fds)
{
#ifdef DEBUG
	printf("VF: Broadcasting ABORT\n");
#endif
	return send_to_all(fds, VF_MESSAGE, VF_ABORT, 0, 0);
}


static int
vf_send_commit(int *fds)
{
#ifdef DEBUG
	printf("VF: Broadcasting FORMED\n");
#endif
	return send_to_all(fds, VF_MESSAGE, VF_VIEW_FORMED, 0, 1);
}


static void
close_all(int *fds)
{
	int x;
	for (x = 0; fds[x] != -1; x++) {
		msg_close(fds[x]);
	}
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
kn_find_fd(uint32_t fd)
{
	key_node_t *cur;
	view_node_t *curvn;

	for (cur = key_list; cur; cur = cur->kn_next)
		for (curvn = cur->kn_jvlist; curvn; curvn = curvn->vn_next)
			if (curvn->vn_fd == fd)
				return cur;

	return NULL;
}


static int
vf_handle_join_view_msg(int fd, vf_msg_t * hdrp)
{
	struct timeval timeout;
	key_node_t *key_node;

#ifdef DEBUG
	printf("VF_JOIN_VIEW from member #%d! Key: %s #%d\n",
	       hdrp->vm_msg.vf_coordinator, hdrp->vm_msg.vf_keyid,
	       (int) hdrp->vm_msg.vf_view);
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
			vf_vote_no(fd);
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
			vf_vote_no(fd);
			return VFR_OK;
		}
	}
	
	/*
	 * Buffer the join-view message.
	 */
	timeout.tv_sec = key_node->kn_tsec;
	timeout.tv_usec = 0;

	if (vf_buffer_join_msg(fd, (vf_msg_t *) hdrp, &timeout)) {
		pthread_mutex_unlock(&key_list_mutex);
#ifdef DEBUG
		printf("VF: Voting YES\n");
#endif
		vf_vote_yes(fd);
		return VFR_OK;
	}

	pthread_mutex_unlock(&key_list_mutex);
#ifdef DEBUG
	printf("VF: Voting NO\n");
#endif
	vf_vote_no(fd);
	return VFR_NO;
}


/*
 * Try to resolve (JOIN_VIEW, FORMED_VIEW) messages in the proper order.
 * Returns the number of commits.
 */
static int
vf_resolve_views(key_node_t *key_node)
{
	int commitfd, commits = 0;
	void *data;
	uint32_t datalen;

	while ((commitfd = vf_try_commit(key_node)) != -1) {

		/* XXX in general, this shouldn't kill the fd... */
		commits++;
		msg_close(commitfd);
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
vf_unanimous(int *peer_fds, int remain, int timeout)
{
	generic_msg_hdr response;
	struct timeval tv;
	fd_set rfds;
	int nready, x;

	/* Set up for the select */
	tv.tv_sec = timeout;
	tv.tv_usec = 0;

	/*
	 * Wait for activity
	 */
	
	/*
	 * Flag hosts which we received messages from so we don't
	 * read a second message.
	 */
	while (remain) {
		FD_ZERO(&rfds);
		for (x = 0; peer_fds[x] != -1; x++)
			FD_SET(peer_fds[x], &rfds);

		nready = select(MAX_FDS, &rfds, NULL, NULL, &tv);
		if (nready <= -1) {
			if (nready == 0)
				printf("VF Abort: Timed out!\n");
			else
				printf("VF Abort: %s\n",
				       strerror(errno));
			return 0;
		}

		for (x = 0; (peer_fds[x] != -1) && nready; x++) {
			if (!FD_ISSET(peer_fds[x], &rfds))
				continue;

			remain--;
			nready--;
			/*
			 * Get reply from node x. XXX 1 second timeout?
			 */
			if (msg_receive_timeout(peer_fds[x], &response,
						sizeof(response),
						1) == -1) {
				printf("VF: Abort: Timed out during "
				       "receive from fd #%d\n", peer_fds[x]);
				return 0;
			}
			
			/*
			 * Decode & validate message
			 */
			swab_generic_msg_hdr(&response);
			if ((response.gh_magic != GENERIC_HDR_MAGIC) ||
			    (response.gh_command != VF_MESSAGE) ||
			    (response.gh_arg1 != VF_VOTE)) {
				printf("VF: Abort: Invalid header in"
				       " reply from fd #%d\n", peer_fds[x]);
				return 0;
			}
			
			/*
			 * If we get a 'NO', we are done.
			 */
			if (response.gh_arg2 != 1) {
				/*
				 * XXX ok, it might be a mangled message;
				 * treat it as no anyway!
				 */
				printf("VF: Abort: fd #%d voted NO\n",
				       peer_fds[x]);
				return 0;
			}

#ifdef DEBUG
			printf("VF: fd #%d voted YES\n", peer_fds[x]);
#endif
		}
	}

	/*
	 * Whohoooooooo!
	 */
	return 1;
}


/*
 * ...
 */
static view_node_t *
vn_new(int fd, uint32_t nodeid, uint64_t viewno, void *data, uint32_t datalen)
{
	view_node_t *new;
	size_t totallen;

	totallen = sizeof(*new) + datalen;
	new = malloc(totallen);
	if (!new)
		return NULL;

	memset(new,0,totallen);

	new->vn_fd = fd;
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
vn_remove(view_node_t **head, int fd)
{
	view_node_t *cur = *head, *back = NULL;

	if (!cur)
		return NULL;

	do {
		if (cur->vn_fd == fd) {
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
vf_buffer_join_msg(int fd, vf_msg_t *hdr, struct timeval *timeout)
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

	newp = vn_new(fd, hdr->vm_msg.vf_coordinator, hdr->vm_msg.vf_view, 
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
	if (left->vc_fd < right->vc_fd)
		return -1;

	if (left->vc_fd == right->vc_fd)
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
vc_remove(commit_node_t **head, int fd)
{
	commit_node_t *cur = *head, *back = NULL;

	if (!cur)
		return NULL;

	do {
		if (cur->vc_fd == fd) {
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
vf_buffer_commit(int fd)
{
	key_node_t *key_node;
	commit_node_t *newp;
	int rv;

	key_node = kn_find_fd(fd);
	if (!key_node)
		return 0;

	newp = malloc(sizeof(*newp));
	if (!newp)
		return 0;

	newp->vc_next = NULL;
	newp->vc_fd = fd;

	rv = vc_insert_sorted(&key_node->kn_clist, newp);
	if (!rv)
		free(newp);

	return rv;
}


static int
vf_vote_yes(int fd)
{
	return msg_send_simple(fd, VF_MESSAGE, VF_VOTE, 1);

}


static int
vf_vote_no(int fd)
{
	return msg_send_simple(fd, VF_MESSAGE, VF_VOTE, 0);
}


static int
vf_abort(int fd)
{
	key_node_t *key_node;
	view_node_t *cur;

	key_node = kn_find_fd(fd);
	if (!key_node)
		return -1;

	cur = vn_remove(&key_node->kn_jvlist, fd);
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

	rv = fscanf(fp,"%ld.%ld %ld.%ld\n", &tv->tv_sec, &tv->tv_usec,
		    &junk.tv_sec, &junk.tv_usec);
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
static int
vf_try_commit(key_node_t *key_node)
{
	view_node_t *vnp;
	commit_node_t *cmp;
	int fd = -1;

	if (!key_node)
		return -1;

	if (!key_node->kn_jvlist)
		return -1;

	fd = key_node->kn_jvlist->vn_fd;
		
	cmp = vc_remove(&key_node->kn_clist, fd);
	if (!cmp) {
		/*printf("VF: Commit for fd%d not received yet!", fd);*/
		return -1;
	}

	free(cmp); /* no need for it any longer */
		
	vnp = vn_remove(&key_node->kn_jvlist, fd);
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
	memcpy(key_node->kn_data, vnp->vn_data, vnp->vn_datalen);

	free(vnp);
	return fd;
}


void
vf_event_loop(uint64_t my_node_id)
{
	int max, nready, n, fd, flags;
	struct timeval tv;
	fd_set rfds;
	generic_msg_hdr *hdrp = NULL;

	FD_ZERO(&rfds);
	max = msg_fill_fdset(&rfds, MSG_ALL, MSGP_VFS);

	tv.tv_sec = 1;
	tv.tv_usec = 0;
	nready = select(max + 1, &rfds, NULL, NULL, &tv);
	if (nready <= 0)
		return;

	while (nready) {
		fd = msg_next_fd(&rfds);
		--nready;

		flags = msg_get_flags(fd);

		if (flags & MSG_LISTEN)
			fd = msg_accept(fd, 1, NULL);

		n = msg_receive_simple(fd, &hdrp, 5);

		if (n <= 0 || !hdrp) {
			msg_close(fd);
			continue;
		}

		swab_generic_msg_hdr(hdrp);
		if (hdrp->gh_command == VF_MESSAGE) {
			if (vf_process_msg(fd, hdrp, n) == VFR_COMMIT) {
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
	while (!thread_ready) {
		pthread_mutex_unlock(&vf_mutex);
		usleep(50000);
		pthread_mutex_lock(&vf_mutex);
	}
	pthread_mutex_unlock(&vf_mutex);
}


void *
vf_server(void *arg)
{
	uint64_t my_node_id;
	uint16_t port;
	key_node_t *cur;
	int fd;

	block_all_signals();

	port = ((struct vf_args *)arg)->port;
	my_node_id = ((struct vf_args *)arg)->local_node_id;
	free(arg);

#ifdef DEBUG
	printf("VFT: Thread id %ld starting\n", (long)pthread_self());
#endif

	pthread_mutex_lock(&vf_mutex);
	if ((vf_lfd = msg_listen(port, MSGP_VFS, vf_lfds, 2)) <= 0) {
		printf("Unable to set up listen socket on port %d\n",
		       port);
		pthread_mutex_unlock(&vf_mutex);
		pthread_exit(NULL);
	}

	thread_ready = 1;
	pthread_mutex_unlock(&vf_mutex);

	while (1) {
		pthread_mutex_lock(&key_list_mutex);
		for (cur = key_list; cur; cur = cur->kn_next) {
			/* Destroy timed-out join views */
			while (_vf_purge(cur, &fd) != VFR_NO) {
				msg_close(fd);
			}
		}
		pthread_mutex_unlock(&key_list_mutex);
		vf_event_loop(my_node_id);
	}
	return NULL;
}



/**
 * Initialize VF.  Initializes the View Formation sub system.
 * @param my_node_id	The node ID of the caller.
 * @param my_port	The port of the caller.
 * @return		0 on success, -1 on failure.
 */
int
vf_init(uint64_t my_node_id, uint16_t my_port, vf_vote_cb_t vcb,
	vf_commit_cb_t ccb)
{
	struct vf_args *va;

	if (my_node_id == (uint64_t)-1)
		return -1;

	if (my_port == 0)
		return -1;

	pthread_mutex_lock(&vf_mutex);
	if (vf_thread != (pthread_t)-1) {
		pthread_mutex_unlock(&vf_mutex);
		return 0;
	}

	va = malloc(sizeof(*va));
	va->local_node_id = my_node_id;
	va->port = my_port;

	pthread_create(&vf_thread, NULL, vf_server, va);

	/* Write/read needs this */
	_port = my_port;
	_node_id = my_node_id;
	default_vote_cb = vcb;
	default_commit_cb = ccb;
	pthread_mutex_unlock(&vf_mutex);

	vf_wait_ready();

	return 0;
}


/**
  Shut down VF
  */
int
vf_shutdown(void)
{
	int x;
	key_node_t *c_key;
	view_node_t *c_jv;
	commit_node_t *c_cn;

	pthread_mutex_lock(&vf_mutex);
	pthread_cancel(vf_thread);
	pthread_join(vf_thread, NULL);
	thread_ready = 0;
	vf_thread = (pthread_t)0;

	for (x = 0 ; x < vf_lfd; x++)
		msg_close(vf_lfds[x]);

	_port = 0;
	_node_id = (uint64_t)-1;
	pthread_mutex_lock(&key_list_mutex);

	while ((c_key = key_list) != NULL) {

		while ((c_jv = c_key->kn_jvlist) != NULL) {
			msg_close(c_jv->vn_fd);
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
		      uint64_t viewno, uint32_t *retlen)
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
	uint64_t nodeid;
	int *peer_fds;
	int count;
	key_node_t *key_node;
	vf_msg_t *join_view;
	int remain = 0, x, y, rv = 1, totallen;
	struct timeval start, end, dif;
	void *lockp = NULL;
	int l;
	char lock_name[256];

	if (!data || !datalen || !keyid || !strlen(keyid) || !membership)
		return -1;

	pthread_mutex_lock(&vf_mutex);
	/* Obtain cluster lock on it. */
	snprintf(lock_name, sizeof(lock_name), "usrm::vf");
	l = clu_lock(lock_name, CLK_EX, &lockp);
	if (l < 0) {
		clu_unlock(lock_name, lockp);
		pthread_mutex_unlock(&vf_mutex);
		return l;
	}

	/* set to -1 */
	count = sizeof(int) * (membership->cml_count + 1);
	peer_fds = malloc(count);
	if(!peer_fds) {
		pthread_mutex_unlock(&vf_mutex);
		return -1;
	}

	for (x=0; x < membership->cml_count + 1; x++)
		peer_fds[x] = -1;
	getuptime(&start);

retry_top:
	/*
	 * Connect to everyone, except ourself.  We separate this from the
	 * initial send cycle because the connect cycle can cause timeouts
	 * within the code - ie, if a node is down, it is likely the connect
	 * will take longer than the client is expecting to wait for the
	 * commit/abort messages!
	 *
	 * We assume we're up.  Since challenge-response needs both
	 * processes to be operational...
	 */
	for (x = 0, y = 0; x < membership->cml_count; x++) {
		if (!memb_online(membership,
				 membership->cml_members[x].cm_id)) {
			continue;
		}

		if (peer_fds[x] != -1)
			continue;

		nodeid = membership->cml_members[x].cm_id;
#ifdef DEBUG
		printf("VF: Connecting to member #%d\n", (int)nodeid);
		fflush(stdout);
#endif
		peer_fds[y] = msg_open(nodeid, _port, MSGP_VFC, 4);

		if (peer_fds[y] == -1) {
#ifdef DEBUG
			printf("VF: Connect to %d failed: %s\n", (int)nodeid,
			       strerror(errno));
#endif
			if (flags & VFF_RETRY)
				goto retry_top;
			if (flags & VFF_IGN_CONN_ERRORS)
				continue;
			free(peer_fds);

			clu_unlock(lock_name, lockp);
			pthread_mutex_unlock(&vf_mutex);
			return -1;
		}

		++y;
	}

	pthread_mutex_lock(&key_list_mutex);
	key_node = kn_find_key(keyid);
	if (!key_node) {

		if ((vf_key_init_nt(keyid, 10, NULL, NULL) < 0)) {
			pthread_mutex_unlock(&key_list_mutex);
			clu_unlock(lock_name, lockp);
			pthread_mutex_unlock(&vf_mutex);
			return -1;
		}
		key_node = kn_find_key(keyid);
		assert(key_node);
	}

	join_view = build_vf_data_message(VF_JOIN_VIEW, keyid, data, datalen,
					  key_node->kn_viewno+1, &totallen);

	pthread_mutex_unlock(&key_list_mutex);

	if (!join_view) {
		clu_unlock(lock_name, lockp);
		pthread_mutex_unlock(&vf_mutex);
		return -1;
	}

#ifdef DEBUG
	printf("VF: Push %d.%d #%d\n", (int)_node_id, getpid(),
	       (int)join_view->vm_msg.vf_view);
#endif
	/* 
	 * Encode the package.
	 */
	swab_vf_msg_t(join_view);

	/*
	 * Send our message to everyone
	 */
	for (x = 0; peer_fds[x] != -1; x++) {

		if (msg_send(peer_fds[x], join_view, totallen) != totallen) {
			vf_send_abort(peer_fds);
			close_all(peer_fds);

			free(join_view);
			clu_unlock(lock_name, lockp);
			pthread_mutex_unlock(&vf_mutex);
			return -1;
		} 

		remain++;
	}

#ifdef DEBUG
	printf("VF: Checking for consensus...\n");
#endif
	/*
	 * See if we have a consensus =)
	 */
	if ((rv = (vf_unanimous(peer_fds, remain, VF_COORD_TIMEOUT)))) {
		vf_send_commit(peer_fds);
	} else {
		vf_send_abort(peer_fds);
#ifdef DEBUG
		printf("VF: Aborted!\n");
#endif
	}

	/*
	 * Clean up
	 */
	close_all(peer_fds);

	/*
	 * unanimous returns 1 for true; 0 for false, so negate it and
	 * return our value...
	 */
	free(join_view);
	free(peer_fds);
	clu_unlock(lock_name, lockp);
	pthread_mutex_unlock(&vf_mutex);

	if (rv) {
		getuptime(&end);

		dif.tv_usec = end.tv_usec - start.tv_usec;
		dif.tv_sec = end.tv_sec - start.tv_sec;
		
		if (dif.tv_usec < 0) {
		    dif.tv_usec += 1000000;
		    dif.tv_sec--;
		}

#ifdef DEBUG
		printf("VF: Converge Time: %d.%06d\n", (int)dif.tv_sec,
		       (int)dif.tv_usec);
#endif
	}

	return (rv?0:-1);
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
_vf_purge(key_node_t *key_node, int *fd)
{
	view_node_t *cur, *dead = NULL;
	struct timeval tv;

	*fd = -1;
	
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

		*fd = cur->vn_fd;
		dead = vn_remove(&key_node->kn_jvlist, *fd);
		free(dead);

		printf("VF: Killed fd %d\n", *fd);
		/*
		 * returns the removed associated file descriptor
		 * so that we can close it and get on with life
		 */
		break;
	}

	if (*fd == -1)
		return VFR_NO;
		
	if (vf_resolve_views(key_node))
		return VFR_COMMIT;
	return VFR_OK;
}


/**
 * Process a VF message.
 *
 * @param handle	File descriptor on which msgp was received.
 * @param msgp		Pointer to already-received message.
 * @param nbytes	Length of msgp.
 * @return		-1 on failure, 0 on success.
 */
int
vf_process_msg(int handle, generic_msg_hdr *msgp, int nbytes)
{
	vf_msg_t *hdrp;
	int ret;

	if ((nbytes <= 0) || (nbytes < sizeof(generic_msg_hdr)) ||
	    (msgp->gh_command != VF_MESSAGE))
		return VFR_ERROR;

	switch(msgp->gh_arg1) {
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

		return vf_send_current(handle, hdrp->vm_msg.vf_keyid);
	
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
		return vf_handle_join_view_msg(handle, hdrp);
		
	case VF_ABORT:
		printf("VF: Received VF_ABORT, fd%d\n", handle);
		vf_abort(handle);
		return VFR_ABORT;
		
	case VF_VIEW_FORMED:
#ifdef DEBUG
		printf("VF: Received VF_VIEW_FORMED, fd%d\n",
		       handle);
#endif
		pthread_mutex_lock(&key_list_mutex);
		vf_buffer_commit(handle);
		ret = (vf_resolve_views(kn_find_fd(handle)) ?
			VFR_COMMIT : VFR_OK);
		pthread_mutex_unlock(&key_list_mutex);
		return ret;
			
	default:
		printf("VF: Unknown msg type 0x%08x\n",
		       msgp->gh_arg1);
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
	void *lockp = NULL;
	int l;

	/* Obtain cluster lock on it. */
	pthread_mutex_lock(&vf_mutex);
	snprintf(lock_name, sizeof(lock_name), "usrm::vf");
	l = clu_lock(lock_name, CLK_EX, &lockp);
	if (l < 0) {
		clu_unlock(lock_name, lockp);
		pthread_mutex_unlock(&vf_mutex);
		printf("Couldn't lock %s\n", keyid);
		return l;
	}

	do {
		pthread_mutex_lock(&key_list_mutex);

		key_node = kn_find_key(keyid);
		if (!key_node) {
			if ((vf_key_init_nt(keyid, 10, NULL, NULL) < 0)) {
				pthread_mutex_unlock(&key_list_mutex);
				clu_unlock(lock_name, lockp);
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
			clu_unlock(lock_name, lockp);
			//printf("Membership NULL, can't find %s\n", keyid);
			pthread_mutex_unlock(&vf_mutex);
			return VFR_ERROR;
		}

		l = vf_request_current(membership, keyid, view, data,
				       datalen);
	       	if (l == VFR_NODATA || l == VFR_ERROR) {
			clu_unlock(lock_name, lockp);
			//printf("Requesting current failed %s %d\n", keyid, l);
			pthread_mutex_unlock(&vf_mutex);
			return l;
		}
	}

	*data = malloc(key_node->kn_datalen);
	if (! *data) {
		pthread_mutex_unlock(&key_list_mutex);
		clu_unlock(lock_name, lockp);
		pthread_mutex_unlock(&vf_mutex);
		printf("Couldn't malloc %s\n", keyid);
		return VFR_ERROR;
	}

	memcpy(*data, key_node->kn_data, key_node->kn_datalen);
	*datalen = key_node->kn_datalen;
	*view = key_node->kn_viewno;

	pthread_mutex_unlock(&key_list_mutex);
	clu_unlock(lock_name, lockp);
	pthread_mutex_unlock(&vf_mutex);

	return VFR_OK;
}


static int
vf_send_current(int fd, char *keyid)
{
	key_node_t *key_node;
	vf_msg_t *msg;
	int totallen, ret;

	if (fd == -1)
		return VFR_ERROR;

	pthread_mutex_lock(&key_list_mutex);

	key_node = kn_find_key(keyid);
	if (!key_node || !key_node->kn_data || !key_node->kn_datalen) {
		pthread_mutex_unlock(&key_list_mutex);
		printf("VFT: No data for keyid %s\n", keyid);
		return (msg_send_simple(fd, VF_NACK, 0, 0) != -1)?
			VFR_OK : VFR_ERROR;
	}

	/*
	 * XXX check for presence of nodes on the commit lists; send
	 * VF_AGAIN if there is any.
	 */
	msg = build_vf_data_message(VF_ACK, keyid, key_node->kn_data,
				    key_node->kn_datalen,
				    key_node->kn_viewno,
				    &totallen);

	pthread_mutex_unlock(&key_list_mutex);
	if (!msg)
		return (msg_send_simple(fd, VFR_ERROR, 0, 0) != -1)?
			VFR_OK : VFR_ERROR;

	swab_vf_msg_t(msg);
	ret = (msg_send(fd, msg, totallen) != -1)?VFR_OK:VFR_ERROR;
	free(msg);
	return ret;
}


static int
vf_set_current(char *keyid, uint64_t view, void *data, uint32_t datalen)
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
	int fd, x, n, rv = VFR_OK, port;
	vf_msg_t rmsg;
	vf_msg_t *msg = &rmsg;
	generic_msg_hdr * gh;
	uint64_t me;

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
		if (!memb_online(membership,
				 membership->cml_members[x].cm_id))
			continue;

		/* Can't request from self. */
		if (membership->cml_members[x].cm_id == me)
			continue;

		rv = VFR_ERROR;
		fd = msg_open(membership->cml_members[x].cm_id, port,
			      MSGP_VFC, 5);
		if (fd == -1)
			continue;

		msg = &rmsg;
		//printf("VF: Requesting current value of %s from %d\n",
		       //msg->vm_msg.vf_keyid,
		       //(int)membership->cml_members[x].cm_id);

		if (msg_send(fd, msg, sizeof(*msg)) != sizeof(*msg)) {
			printf("Couldn't send entire message\n");
			continue;
		}

		gh = NULL;
		if ((n = msg_receive_simple(fd, (generic_msg_hdr **)&gh, 10))
		    == -1) {
			if (gh)
				free(gh);
			msg_close(fd);
			continue;
		}
		msg_close(fd);
		msg = (vf_msg_t *)gh;
		break;
	}

	if (x >= membership->cml_count)
		return VFR_ERROR;

	/* Uh oh */
	if (!msg || (msg == &rmsg)) {
		printf("VF: No valid message\n");
		return VFR_ERROR;
	}
		
	swab_generic_msg_hdr(&(msg->vm_hdr));
	if (msg->vm_hdr.gh_command == VF_NACK) {
		free(msg);
		return VFR_NODATA;
	}

	if (msg->vm_hdr.gh_length < sizeof(vf_msg_t)) {
		fprintf(stderr, "VF: Short reply from %d\n", x);
		free(msg);
		return VFR_ERROR;
	}

	if (msg->vm_hdr.gh_length > n) {
		fprintf(stderr,"VF: Size mismatch during decode (%d > %d)\n",
			msg->vm_hdr.gh_length, n);
		free(msg);
		return VFR_ERROR;
	}

	swab_vf_msg_info_t(&(msg->vm_msg));

	if (msg->vm_msg.vf_datalen != (n - sizeof(*msg))) {
		fprintf(stderr,"VF: Size mismatch during decode (\n");
		free(msg);
		return VFR_ERROR;
	}

	if (vf_set_current(keyid, msg->vm_msg.vf_view,
			   msg->vm_msg.vf_data,
			   msg->vm_msg.vf_datalen) == VFR_ERROR) {
		free(msg);
		return VFR_ERROR;
	}

	free(msg);

	return VFR_OK;
}

