#include <message.h>
#include <platform.h>
#include <ccs.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <rg_locks.h>
#include <fcntl.h>
#include <resgroup.h>
#include <clulog.h>
#include <members.h>
#include <msgsimple.h>
#include <vf.h>
#include <lock.h>
#include <rg_queue.h>
#include <malloc.h>
#include <cman-private.h>
#include <event.h>

#define L_SHUTDOWN (1<<2)
#define L_SYS (1<<1)
#define L_USER (1<<0)

#ifdef WRAP_THREADS
void dump_thread_states(FILE *);
#endif
int configure_rgmanager(int ccsfd, int debug);
void set_transition_throttling(int);

void node_event(int, int, int, int);
void node_event_q(int, int, int, int);
int daemon_init(char *);
int init_resource_groups(int, int);
void kill_resource_groups(void);
void set_my_id(int);
void flag_shutdown(int sig);
void hard_exit(void);
int send_rg_states(msgctx_t *, int);
int check_config_update(int *, int *);
int svc_exists(char *);
int watchdog_init(void);
int32_t master_event_callback(char *key, uint64_t viewno, void *data, uint32_t datalen);

int node_has_fencing(int nodeid);
int fence_domain_joined(void);

int shutdown_pending = 0, running = 1, need_reconfigure = 0;
char debug = 0; /* XXX* */
static int signalled = 0;
static uint8_t ALIGNED port = RG_PORT;
static char *rgmanager_lsname = "rgmanager"; /* XXX default */
static int status_poll_interval = DEFAULT_CHECK_INTERVAL;

int next_node_id(cluster_member_list_t *membership, int me);
void malloc_dump_table(FILE *, size_t, size_t);

void
segfault(int __attribute__ ((unused)) sig)
{
	char ow[64];
	int err; // dumb error checking... will be replaced by logsys

	snprintf(ow, sizeof(ow)-1, "PID %d Thread %d: SIGSEGV\n", getpid(),
		 gettid());
	err = write(2, ow, strlen(ow));
	while(1)
		sleep(60);
}


int
send_exit_msg(msgctx_t *ctx)
{
	msg_send_simple(ctx, RG_EXITING, my_id(), 0);

	return 0;
}


void
send_node_states(msgctx_t *ctx)
{
	int x;
	event_master_t master;
	generic_msg_hdr hdr;
	cluster_member_list_t *ml = member_list();

	master.m_nodeid = 0;
	event_master_info_cached(&master);

	for (x = 0; x < ml->cml_count; x++) {
		if (ml->cml_members[x].cn_member == 1) {
			msg_send_simple(ctx, RG_STATUS_NODE,
					ml->cml_members[x].cn_nodeid, 
					(ml->cml_members[x].cn_nodeid &&
					 (ml->cml_members[x].cn_nodeid == 
					  (int)master.m_nodeid)));
		}
	}
	msg_send_simple(ctx, RG_SUCCESS, 0, 0);
	msg_receive(ctx, &hdr, sizeof(hdr), 10);
	free_member_list(ml);
}


void
flag_reconfigure(int __attribute__ ((unused)) sig)
{
	need_reconfigure++;
}


/**
  This updates our local membership view and handles whether or not we
  should exit, as well as determines node transitions (thus, calling
  node_event()).
 
  @see				node_event
  @return			0
 */
int
membership_update(void)
{
	cluster_member_list_t *new_ml = NULL, *node_delta = NULL,
			      *old_membership = NULL;
	int		x;
	int		me = 0;
	cman_handle_t 	h;
	int 		quorate;

	h = cman_init(NULL);
	quorate = cman_is_quorate(h);
	if (!quorate) {
		cman_finish(h);

		if (!rg_quorate())
			return -1;

		clulog(LOG_EMERG, "#1: Quorum Dissolved\n");
		rg_set_inquorate();
		member_list_update(NULL);/* Clear member list */
		rg_lockall(L_SYS);
		rg_doall(RG_INIT, 1, "Emergency stop of %s\n");
#ifndef USE_OPENAIS
		clulog(LOG_DEBUG, "Invalidating local VF cache\n");
		vf_invalidate();
#endif
		clulog(LOG_DEBUG, "Flushing resource group cache\n");
		kill_resource_groups();
		rg_set_uninitialized();
		return -1;
	} else if (!rg_quorate()) {

		rg_set_quorate();
		rg_unlockall(L_SYS);
		rg_unlockall(L_USER);
		clulog(LOG_NOTICE, "Quorum Regained\n");
	}

	old_membership = member_list();
	new_ml = get_member_list(h);
	memb_mark_down(new_ml, 0);

	for(x=0; new_ml && x<new_ml->cml_count;x++) {
		if (new_ml->cml_members[x].cn_nodeid == 0) {
		    new_ml->cml_members[x].cn_member = 0;
		}
	}

	for (x = 0; new_ml && x < new_ml->cml_count; x++) {

		if (new_ml->cml_members[x].cn_member == 0) {
			printf("skipping %d - node not member\n",
			       new_ml->cml_members[x].cn_nodeid);
			continue;
		}
		if (new_ml->cml_members[x].cn_nodeid == my_id())
			continue;

#ifdef DEBUG
		printf("Checking for listening status of %d\n",
		       new_ml->cml_members[x].cn_nodeid);
#endif

		do {
			quorate = cman_is_listening(h,
					new_ml->cml_members[x].cn_nodeid,
					port);

			if (quorate == 0) {
				clulog(LOG_DEBUG, "Node %d is not listening\n",
					new_ml->cml_members[x].cn_nodeid);
				new_ml->cml_members[x].cn_member = 0;
				break;
			} else if (quorate < 0) {
				if (errno == ENOTCONN) {
					new_ml->cml_members[x].cn_member = 0;
					break;
				}
				perror("cman_is_listening");
				usleep(50000);
				continue;
			}
#ifdef DEBUG
		       	else {
				printf("Node %d IS listening\n",
				       new_ml->cml_members[x].cn_nodeid);
			}
#endif
			break;
		} while(1);
	}

	cman_finish(h);
	member_list_update(new_ml);

	/*
	 * Handle nodes lost.  Do our local node event first.
	 */
	node_delta = memb_lost(old_membership, new_ml);

	me = memb_online(node_delta, my_id());
	if (me) {
		/* Should not happen */
		clulog(LOG_INFO, "State change: LOCAL OFFLINE\n");
		if (node_delta)
			free_member_list(node_delta);
		node_event(1, my_id(), 0, 0);
		/* NOTREACHED */
	}

	for (x=0; node_delta && x < node_delta->cml_count; x++) {

		clulog(LOG_INFO, "State change: %s DOWN\n",
		       node_delta->cml_members[x].cn_name);
		/* Don't bother evaluating anything resource groups are
		   locked.  This is just a performance thing */
		if (!rg_locked()) {
			node_event_q(0, node_delta->cml_members[x].cn_nodeid,
				     0, 0);
		} else {
			clulog(LOG_DEBUG, "Not taking action - services"
			       " locked\n");
		}
	}

	free_member_list(node_delta);

	/*
	 * Handle nodes gained.  Do our local node event first.
	 */
	node_delta = memb_gained(old_membership, new_ml);

	me = memb_online(node_delta, my_id());
	if (me) {
		clulog(LOG_INFO, "State change: Local UP\n");
		node_event_q(1, my_id(), 1, 1);
	}

	for (x=0; node_delta && x < node_delta->cml_count; x++) {

		if (!node_delta->cml_members[x].cn_member)
			continue;

		if (node_delta->cml_members[x].cn_nodeid == my_id())
			continue;

		clulog(LOG_INFO, "State change: %s UP\n",
		       node_delta->cml_members[x].cn_name);
		node_event_q(0, node_delta->cml_members[x].cn_nodeid, 1, 1);
	}

	free_member_list(node_delta);
	free_member_list(new_ml);
	free_member_list(old_membership);

	rg_unlockall(L_SYS);

	return 0;
}


int
lock_commit_cb(char __attribute__ ((unused)) *key,
	       uint64_t __attribute__ ((unused)) viewno,
	       void *data, uint32_t datalen)
{
	char lockstate;

	if (datalen != 1) {
		clulog(LOG_WARNING, "%s: invalid data length!\n", __FUNCTION__);
		free(data);
		return 0;
	}

       	lockstate = *(char *)data;
	free(data);

	if (lockstate == 0) {
		rg_unlockall(L_USER); /* Doing this multiple times
					 has no effect */
		clulog(LOG_NOTICE, "Resource Groups Unlocked\n");
		return 0;
	}

	if (lockstate == 1) {
		rg_lockall(L_USER); /* Doing this multiple times
				       has no effect */
		clulog(LOG_NOTICE, "Resource Groups Locked\n");
		return 0;
	}

	clulog(LOG_DEBUG, "Invalid lock state in callback: %d\n", lockstate);
	return 0;
}


#if 0
struct lr_arg {
	msgctx_t *ctx;
	int req;
};


void *
lockreq_th(void *a)
{
	int ret;
	char state;
	struct lr_arg *lr_arg = (struct lr_arg *)a;
	cluster_member_list_t *m = member_list();

	state = (lr_arg->req==RG_LOCK)?1:0;
	ret = vf_write(m, VFF_IGN_CONN_ERRORS, "rg_lockdown", &state, 1);
	free_member_list(m);

	if (ret == 0) {
		msg_send_simple(lr_arg->ctx, RG_SUCCESS, 0, 0);
	} else {
		msg_send_simple(lr_arg->ctx, RG_FAIL, 0, 0);
	}

	msg_close(lr_arg->ctx);
	msg_free_ctx(lr_arg->ctx);
	free(lr_arg);
	return NULL;
}


void
do_lockreq(msgctx_t *ctx, int req)
{
	pthread_t th;
	struct lr_arg *arg;

	arg = malloc(sizeof(*arg));
	if (!arg) {
		msg_send_simple(ctx, RG_FAIL, 0, 0);
		msg_close(ctx);
		msg_free_ctx(ctx);
		return 0;
	}

	arg->ctx = ctx;
	arg->req = req;

	pthread_create(&th, NULL, lockreq_th, (void *)arg);
}
#else
void 
do_lockreq(msgctx_t *ctx, int req)
{
	int ret;
	char state;
#ifdef OPENAIS
	msgctx_t everyone;
#else
	cluster_member_list_t *m = member_list();
#endif

	state = (req==RG_LOCK)?1:0;

#ifdef OPENAIS
	ret = ds_write("rg_lockdown", &state, 1);
	clulog(LOG_INFO, "FIXME: send RG_LOCK update to all!\n");
#else
	ret = vf_write(m, VFF_IGN_CONN_ERRORS, "rg_lockdown", &state, 1);
	free_member_list(m);
#endif

	if (ret == 0) {
		msg_send_simple(ctx, RG_SUCCESS, 0, 0);
	} else {
		msg_send_simple(ctx, RG_FAIL, 0, 0);
	}
}
#endif



/**
 * Receive and process a message on a file descriptor and decide what to
 * do with it.  This function doesn't handle messages from the quorum daemon.
 *
 * @param fd		File descriptor with a waiting message.S
 * @return		-1 - failed to receive/handle message, or invalid
 *			data received.  0 - handled message successfully.
 * @see			quorum_msg
 */
int
dispatch_msg(msgctx_t *ctx, int nodeid, int need_close)
{
	int ret = 0, sz = -1, nid;
	char msgbuf[4096];
	generic_msg_hdr	*msg_hdr = (generic_msg_hdr *)msgbuf;
	SmMessageSt	*msg_sm = (SmMessageSt *)msgbuf;

	memset(msgbuf, 0, sizeof(msgbuf));

	/* Peek-a-boo */
	sz = msg_receive(ctx, msg_hdr, sizeof(msgbuf), 1);
	if (sz < (int)sizeof (generic_msg_hdr)) {
		clulog(LOG_ERR,
		       "#37: Error receiving header from %d sz=%d CTX %p\n",
		       nodeid, sz, ctx);
		goto out;
	}

	if (sz < 0)
		return -1;

	if (sz > (int)sizeof(msgbuf)) {
		raise(SIGSTOP);
	}

	/*
	printf("RECEIVED %d %d %d %p\n", sz, (int)sizeof(msgbuf),
	       (int)sizeof(generic_msg_hdr), ctx);
	msg_print(ctx);
	 */

	/* Decode the header */
	swab_generic_msg_hdr(msg_hdr);
	if ((msg_hdr->gh_magic != GENERIC_HDR_MAGIC)) {
		clulog(LOG_ERR,
		       "#38: Invalid magic: Wanted 0x%08x, got 0x%08x\n",
		       GENERIC_HDR_MAGIC, msg_hdr->gh_magic);
		goto out;
	}

	if ((int)msg_hdr->gh_length != sz) {
		clulog(LOG_ERR, "#XX: Read size mismatch: %d %d\n",
		       ret, msg_hdr->gh_length);
		goto out;
	}

	switch (msg_hdr->gh_command) {
	case RG_STATUS:
		//clulog(LOG_DEBUG, "Sending service states to CTX%p\n",ctx);
		if (send_rg_states(ctx, msg_hdr->gh_arg1) == 0)
			need_close = 0;
		break;

	case RG_STATUS_NODE:
		//clulog(LOG_DEBUG, "Sending node states to CTX%p\n",ctx);
		send_node_states(ctx);
		break;

	case RG_LOCK:
	case RG_UNLOCK:
		if (rg_quorate())
			do_lockreq(ctx, msg_hdr->gh_command);
		break;

	case RG_QUERY_LOCK:
		if (rg_quorate()) {
			ret = (rg_locked() & L_USER) ? RG_LOCK : RG_UNLOCK;
			msg_send_simple(ctx, ret, 0, 0);
		}
		break;

	case RG_ACTION_REQUEST:

		if (sz < (int)sizeof(msg_sm)) {
			clulog(LOG_ERR,
			       "#39: Error receiving entire request (%d/%d)\n",
			       ret, (int)sizeof(msg_sm));
			ret = -1;
			goto out;
		}

		/* XXX perf: reencode header */
		swab_generic_msg_hdr(msg_hdr);
		/* Decode SmMessageSt message */
		swab_SmMessageSt(msg_sm);

		if (!svc_exists(msg_sm->sm_data.d_svcName)) {
			msg_sm->sm_data.d_ret = RG_ENOSERVICE;
			/* No such service! */
			swab_SmMessageSt(msg_sm);

			if (msg_send(ctx, msg_sm, sizeof (SmMessageSt)) <
		    	    (int)sizeof (SmMessageSt))
				clulog(LOG_ERR, "#40: Error replying to "
				       "action request.\n");
			ret = -1;
			goto out;
		}

		if (central_events_enabled() &&
		    msg_sm->sm_hdr.gh_arg1 != RG_ACTION_MASTER) {
			
			/* Centralized processing or request is from
			   clusvcadm */
			nid = event_master();
			if (nid != my_id()) {
				/* Forward the message to the event master */
				forward_message(ctx, msg_sm, nid);
			} else {
				/* for us: queue it */
				user_event_q(msg_sm->sm_data.d_svcName,
					     msg_sm->sm_data.d_action,
					     msg_sm->sm_hdr.gh_arg1,
					     msg_sm->sm_hdr.gh_arg2,
					     msg_sm->sm_data.d_svcOwner,
					     ctx);
			}

			return 0;
		}

		/* Distributed processing and/or request is from master node
		   -- Queue request */
		rt_enqueue_request(msg_sm->sm_data.d_svcName,
		  		   msg_sm->sm_data.d_action,
		  		   ctx, 0, msg_sm->sm_data.d_svcOwner,
		  		   msg_sm->sm_hdr.gh_arg1,
		  		   msg_sm->sm_hdr.gh_arg2);
		return 0;

	case RG_EVENT:
		/* Service event.  Run a dependency check */
		if (sz < (int)sizeof(msg_sm)) {
			clulog(LOG_ERR,
			       "#39: Error receiving entire request (%d/%d)\n",
			       ret, (int)sizeof(msg_sm));
			ret = -1;
			goto out;
		}

		/* XXX perf: reencode header */
		swab_generic_msg_hdr(msg_hdr);
		/* Decode SmMessageSt message */
		swab_SmMessageSt(msg_sm);

		/* Send to our rg event handler */
		rg_event_q(msg_sm->sm_data.d_svcName,
			   msg_sm->sm_data.d_action,
			   msg_sm->sm_hdr.gh_arg1,
			   msg_sm->sm_hdr.gh_arg2);
		break;

	case RG_EXITING:
		if (!member_online(msg_hdr->gh_arg1))
			break;

		clulog(LOG_NOTICE, "Member %d shutting down\n",
		       msg_hdr->gh_arg1);
	       	member_set_state(msg_hdr->gh_arg1, 0);
		node_event_q(0, msg_hdr->gh_arg1, 0, 1);
		break;

	case VF_MESSAGE:
		/* Ignore; our VF thread handles these
		    - except for VF_CURRENT XXX (bad design) */
		if (msg_hdr->gh_arg1 == VF_CURRENT)
			vf_process_msg(ctx, 0, msg_hdr, sz);
		break;

	default:
		clulog(LOG_DEBUG, "unhandled message request %d\n",
		       msg_hdr->gh_command);
		break;
	}
out:
	if (need_close) {
		msg_close(ctx);
		msg_free_ctx(ctx);
	}
	return ret;
}

/**
  Grab an event off of the designated context

  @param fd		File descriptor to check
  @return		Event
 */
int
handle_cluster_event(msgctx_t *ctx)
{
	int ret;
	msgctx_t *newctx;
	int nodeid;
	
	ret = msg_wait(ctx, 0);

	switch(ret) {
	case M_PORTOPENED:
		msg_receive(ctx, NULL, 0, 0);
		clulog(LOG_DEBUG, "Event: Port Opened\n");
		membership_update();
		break;
	case M_PORTCLOSED:
		/* Might want to handle powerclosed like membership change */
		msg_receive(ctx, NULL, 0, 0);
		clulog(LOG_DEBUG, "Event: Port Closed\n");
		membership_update();
		break;
	case M_NONE:
		msg_receive(ctx, NULL, 0, 0);
		clulog(LOG_DEBUG, "NULL cluster message\n");
		break;
	case M_OPEN:
		newctx = msg_new_ctx();
		if (msg_accept(ctx, newctx) >= 0 &&
		    rg_quorate()) {
			/* Handle message */
			/* When request completes, the fd is closed */
			nodeid = msg_get_nodeid(newctx);
			dispatch_msg(newctx, nodeid, 1);
			break;
		}
		break;

	case M_DATA:
		nodeid = msg_get_nodeid(ctx);
		dispatch_msg(ctx, nodeid, 0);
		break;
		
	case M_OPEN_ACK:
	case M_CLOSE:
		clulog(LOG_DEBUG, "I should NOT get here: %d\n",
		       ret);
		break;
	case M_STATECHANGE:
		msg_receive(ctx, NULL, 0, 0);
		clulog(LOG_DEBUG, "Membership Change Event\n");
		if (running) {
			rg_unlockall(L_SYS);
			membership_update();
		}
		break;
	case 998:
		break;
	case 999:
	case M_TRY_SHUTDOWN:
		msg_receive(ctx, NULL, 0, 0);
		clulog(LOG_WARNING, "#67: Shutting down uncleanly\n");
		rg_set_inquorate();
		rg_doall(RG_INIT, 1, "Emergency stop of %s");
		rg_set_uninitialized();
#if defined(LIBCMAN_VERSION) && LIBCMAN_VERSION >= 2
		/* cman_replyto_shutdown() */
#endif
		running = 0;
		break;
	}

	return ret;
}


void dump_threads(FILE *fp);
void dump_config_version(FILE *fp);
void dump_vf_states(FILE *fp);
void dump_cluster_ctx(FILE *fp);

void
dump_internal_state(char *loc)
{
	FILE *fp;
	fp=fopen(loc, "w+");
 	dump_config_version(fp);
 	dump_threads(fp);
 	dump_vf_states(fp);
#ifdef WRAP_THREADS
	dump_thread_states(fp);
#endif
	dump_cluster_ctx(fp);
	//malloc_dump_table(fp, 1, 16384); /* Only works if alloc.c us used */
 	fclose(fp);
}

int
event_loop(msgctx_t *localctx, msgctx_t *clusterctx)
{
 	int n = 0, max, ret, oldver, newver;
	fd_set rfds;
	msgctx_t *newctx;
	struct timeval tv;
	int nodeid;

	tv.tv_sec = status_poll_interval;
	tv.tv_usec = 0;

	if (signalled) {
		signalled = 0;
 
		dump_internal_state("/tmp/rgmanager-dump");
	}

	while (running && (tv.tv_sec || tv.tv_usec)) {
		FD_ZERO(&rfds);
		max = -1;
		msg_fd_set(clusterctx, &rfds, &max);
		msg_fd_set(localctx, &rfds, &max);

		n = select(max + 1, &rfds, NULL, NULL, &tv);

		if (n <= 0)
			break;

		if (msg_fd_isset(clusterctx, &rfds)) {
			msg_fd_clr(clusterctx, &rfds);
			handle_cluster_event(clusterctx);
			continue;
		}

		if (!msg_fd_isset(localctx, &rfds)) {
			continue;
		}

		msg_fd_clr(localctx, &rfds);
		newctx = msg_new_ctx();
		ret = msg_accept(localctx, newctx);

		if (ret == -1)
			continue;

		if (rg_quorate()) {
			/* Handle message */
			/* When request completes, the fd is closed */
			nodeid = msg_get_nodeid(newctx);
			dispatch_msg(newctx, nodeid, 1);
			continue;
		}
			
		if (!rg_initialized()) {
			msg_send_simple(newctx, RG_FAIL, RG_EQUORUM, 0);
			msg_close(newctx);
			msg_free_ctx(newctx);
			continue;
		}

		if (!rg_quorate()) {
			printf("Dropping connect: NO QUORUM\n");
			msg_send_simple(newctx, RG_FAIL, RG_EQUORUM, 0);
			msg_close(newctx);
			msg_free_ctx(newctx);
		}
	}

	if (!running)
		return 0;

	if (need_reconfigure || check_config_update(&oldver, &newver)) {
		need_reconfigure = 0;
		configure_rgmanager(-1, 0);
		config_event_q(oldver, newver);
		return 0;
	}

	/* Did we receive a SIGTERM? */
	if (n < 0)
		return 0;

	/* No new messages.  Drop in the status check requests.  */
	if (n == 0 && rg_quorate()) {
		do_status_checks();
		return 0;
	}

	return 0;
}


void
flag_shutdown(int __attribute__ ((unused)) sig)
{
	shutdown_pending = 1;
}


void
hard_exit(void)
{
	rg_lockall(L_SYS);
	rg_doall(RG_INIT, 1, "Emergency stop of %s");
	//vf_shutdown();
	exit(1);
}


void
cleanup(msgctx_t *clusterctx)
{
	kill_resource_groups();
	send_exit_msg(clusterctx);
}



void
statedump(int __attribute__ ((unused)) sig)
{
	signalled++;
}


void malloc_dump_table(FILE *, size_t, size_t);


/*
 * Configure logging based on data in cluster.conf
 */
int
configure_rgmanager(int ccsfd, int dbg)
{
	char *v;
	char internal = 0;

	if (ccsfd < 0) {
		internal = 1;
		ccsfd = ccs_connect();
		if (ccsfd < 0)
			return -1;
	}

	if (ccs_get(ccsfd, "/cluster/rm/@log_facility", &v) == 0) {
		clu_set_facility(v);
		free(v);
	}

	if (ccs_get(ccsfd, "/cluster/rm/@log_level", &v) == 0) {
		if (!dbg)
			clu_set_loglevel(atoi(v));
		free(v);
	}

	if (ccs_get(ccsfd, "/cluster/rm/@transition_throttling", &v) == 0) {
		set_transition_throttling(atoi(v));
		free(v);
	}

	if (ccs_get(ccsfd, "/cluster/rm/@central_processing", &v) == 0) {
		set_central_events(atoi(v));
		if (atoi(v))
			clulog(LOG_NOTICE,
			       "Centralized Event Processing enabled\n");
		free(v);
	}

	if (ccs_get(ccsfd, "/cluster/rm/@status_poll_interval", &v) == 0) {
		status_poll_interval = atoi(v);
		if (status_poll_interval >= 1) {
			clulog(LOG_NOTICE,
			       "Status Polling Interval set to %d\n", v);
		} else {
			clulog(LOG_WARNING, "Ignoring illegal "
			       "status_poll_interval of %s\n", v);
			status_poll_interval = 10;
		}
		
		free(v);
	}


	if (internal)
		ccs_disconnect(ccsfd);

	return 0;
}


void
clu_initialize(cman_handle_t *ch)
{
	if (!ch)
		exit(1);

	*ch = cman_init(NULL);
	if (!(*ch)) {
		clulog(LOG_NOTICE, "Waiting for CMAN to start\n");

		while (!(*ch = cman_init(NULL))) {
			sleep(1);
		}
	}

        if (!cman_is_quorate(*ch)) {
		/*
		   There are two ways to do this; this happens to be the simpler
		   of the two.  The other method is to join with a NULL group 
		   and log in -- this will cause the plugin to not select any
		   node group (if any exist).
		 */
		clulog(LOG_NOTICE, "Waiting for quorum to form\n");

		while (cman_is_quorate(*ch) == 0) {
			sleep(1);
		}
		clulog(LOG_NOTICE, "Quorum formed\n");
	}

}


void
wait_for_fencing(void)
{
        if (node_has_fencing(my_id()) && !fence_domain_joined()) {
		clulog(LOG_INFO, "Waiting for fence domain join operation "
		       "to complete\n");

		while (fence_domain_joined() == 0)
			sleep(1);
		clulog(LOG_INFO, "Fence domain joined\n");
	} else {
		clulog(LOG_DEBUG, "Fence domain already joined "
		       "or no fencing configured\n");
	}
}


void
set_nonblock(int fd)
{
       int flags;

       flags = fcntl(fd, F_GETFL, 0);
       fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}


void *
shutdown_thread(void __attribute__ ((unused)) *arg)
{
	rg_lockall(L_SYS|L_SHUTDOWN);
	rg_doall(RG_STOP_EXITING, 1, NULL);
	running = 0;

	pthread_exit(NULL);
}


#ifdef WRAP_THREADS
void dump_thread_states(FILE *);
#endif
int
main(int argc, char **argv)
{
	int rv, do_init = 1;
	char foreground = 0, wd = 1;
	cman_node_t me;
	msgctx_t *cluster_ctx;
	msgctx_t *local_ctx;
	pthread_t th;
	cman_handle_t clu = NULL;

	while ((rv = getopt(argc, argv, "wfdN")) != EOF) {
		switch (rv) {
		case 'w':
			wd = 0;
			break;
		case 'd':
			debug = 1;
			break;
		case 'N':
			do_init = 0;
			break;
		case 'f':
			foreground = 1;
			break;
		default:
			return 1;
			break;
		}
	}

	/*
	   Set up logging / foreground mode, etc.
	 */
	if (debug)
		clu_set_loglevel(LOG_DEBUG);
	if (foreground)
		clu_log_console(1);

	if (!foreground && (geteuid() == 0)) {
		daemon_init(argv[0]);
		if (wd && !debug && !watchdog_init())
			clulog(LOG_NOTICE, "Failed to start watchdog\n");
	}

	setup_signal(SIGINT, flag_shutdown);
	setup_signal(SIGTERM, flag_shutdown);
	setup_signal(SIGUSR1, statedump);
	unblock_signal(SIGCHLD);
	setup_signal(SIGPIPE, SIG_IGN);

	if (debug) {
		setup_signal(SIGSEGV, segfault);
	} else {
		unblock_signal(SIGSEGV);
	}

	clu_initialize(&clu);
	if (cman_init_subsys(clu) < 0) {
		perror("cman_init_subsys");
		return -1;
	}

	if (clu_lock_init(rgmanager_lsname) != 0) {
		printf("Locks not working!\n");
		return -1;
	}

	memset(&me, 0, sizeof(me));
        cman_get_node(clu, CMAN_NODEID_US, &me);

	if (me.cn_nodeid == 0) {
		printf("Unable to determine local node ID\n");
		perror("cman_get_node");
		return -1;
	}
	set_my_id(me.cn_nodeid);

	clulog(LOG_INFO, "I am node #%d\n", my_id());

	wait_for_fencing();

	/*
	   We know we're quorate.  At this point, we need to
	   read the resource group trees from ccsd.
	 */
	configure_rgmanager(-1, debug);
	clulog(LOG_NOTICE, "Resource Group Manager Starting\n");

	if (init_resource_groups(0, do_init) != 0) {
		clulog(LOG_CRIT, "#8: Couldn't initialize services\n");
		return -1;
	}

	if (msg_listen(MSG_SOCKET, RGMGR_SOCK, me.cn_nodeid, &local_ctx) < 0) {
		clulog(LOG_CRIT,
		       "#10: Couldn't set up cluster message system: %s\n",
		       strerror(errno));
		return -1;
	}

	if (msg_listen(MSG_CLUSTER, &port, me.cn_nodeid, &cluster_ctx) < 0) {
		clulog(LOG_CRIT,
		       "#10b: Couldn't set up cluster message system: %s\n",
		       strerror(errno));
		return -1;
	}

	rg_set_quorate();

	/*
	msg_print(local_ctx);
	msg_print(cluster_ctx);
	 */

	/*
	   Initialize the VF stuff.
	 */
#ifdef OPENAIS
	if (ds_init() < 0) {
		clulog(LOG_CRIT, "#11b: Couldn't initialize SAI AIS CKPT\n");
		return -1;
	}

	ds_key_init("rg_lockdown", 32, 10);
#else
	if (vf_init(me.cn_nodeid, port, NULL, NULL) != 0) {
		clulog(LOG_CRIT, "#11: Couldn't set up VF listen socket\n");
		return -1;
	}

	vf_key_init("rg_lockdown", 10, NULL, lock_commit_cb);
	vf_key_init("Transition-Master", 10, NULL, master_event_callback);
#endif

	/*
	   Do everything useful
	 */
	while (running) {
		event_loop(local_ctx, cluster_ctx);

		if (shutdown_pending == 1) {
			/* Kill local socket; local requests need to
			   be ignored here */
			msg_close(local_ctx);
			++shutdown_pending;
			clulog(LOG_NOTICE, "Shutting down\n");
			pthread_create(&th, NULL, shutdown_thread, NULL);
		}
	}

	if (rg_initialized())
		cleanup(cluster_ctx);
	clulog(LOG_NOTICE, "Shutdown complete, exiting\n");
	clu_lock_finished(rgmanager_lsname);
	cman_finish(clu);
	
	/*malloc_dump_table(); */ /* Only works if alloc.c us used */
	/*malloc_stats();*/

	exit(0);
}
