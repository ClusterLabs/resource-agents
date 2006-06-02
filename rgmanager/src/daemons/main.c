/*
  Copyright Red Hat, Inc. 2002-2004
  Copyright Mission Critical Linux, Inc. 2000

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
#include <rg_queue.h>
#include <malloc.h>

#define L_SYS (1<<1)
#define L_USER (1<<0)

int configure_logging(int ccsfd);

void node_event_q(int, uint64_t, int);
int daemon_init(char *);
int init_resource_groups(int);
void kill_resource_groups(void);
void set_my_id(uint64_t);
int eval_groups(int, uint64_t, int);
void graceful_exit(int);
void flag_shutdown(int sig);
void hard_exit(void);
int send_rg_states(msgctx_t *, int);
int check_config_update(void);
int svc_exists(char *);
int watchdog_init(void);

int shutdown_pending = 0, running = 1, need_reconfigure = 0;
char debug = 0; /* XXX* */
static int signalled = 0;

uint64_t next_node_id(cluster_member_list_t *membership, uint64_t me);


void
segfault(int sig)
{
	char ow[64];

	snprintf(ow, sizeof(ow)-1, "PID %d Thread %d: SIGSEGV\n", getpid(),
		 gettid());
	write(2, ow, strlen(ow));
	while(1)
		sleep(60);
}


int
send_exit_msg(uint64_t nodeid)
{
	msgctx_t ctx;

	if (msg_open(nodeid, RG_PORT, &ctx, 5) < 0) {
		printf("Failed to send exit message\n");
		return -1;
	}
	msg_send_simple(&ctx, RG_EXITING, 0, 0);
	msg_close(&ctx);

	return 0;
}


/**
  Notify other resource group managers that we're leaving, since
  cluster membership is not necessarily tied to the members running
  the rgmgr.
 */
int
notify_exiting(void)
{
	int x;
	uint64_t partner;
	cluster_member_list_t *membership;

	membership = member_list();
	if (!membership)
		return 0;

	for (x = 0; x < membership->cml_count; x++) {

		partner = membership->cml_members[x].cn_nodeid;

		if (partner == my_id() ||
		    !membership->cml_members[x].cn_member)
			continue;

		send_exit_msg(partner);
	}

	free_member_list(membership);

	return 0;
}


void
flag_reconfigure(int sig)
{
	need_reconfigure++;
}


/**
  Called to handle the transition of a cluster member from up->down or
  down->up.  This handles initializing services (in the local node-up case),
  exiting due to loss of quorum (local node-down), and service fail-over
  (remote node down).
 
  @param nodeID		ID of the member which has come up/gone down.
  @param nodeStatus		New state of the member in question.
  @see eval_groups
 */
void
node_event(int local, uint64_t nodeID, int nodeStatus)
{
	if (!running)
		return;

	if (local) {

		/* Local Node Event */
		if (nodeStatus == 0)
			hard_exit();

		if (!rg_initialized()) {
			if (init_resource_groups(0) != 0) {
				clulog(LOG_ERR,
				       "#36: Cannot initialize services\n");
				hard_exit();
			}
		}

		if (shutdown_pending) {
			clulog(LOG_NOTICE, "Processing delayed exit signal\n");
			graceful_exit(SIGINT);
		}
		setup_signal(SIGINT, graceful_exit);
		setup_signal(SIGTERM, graceful_exit);
		setup_signal(SIGHUP, flag_reconfigure);

		eval_groups(1, nodeID, 1);
		return;
	}

	/*
	 * Nothing to do for events from other nodes if we are not ready.
	 */
	if (!rg_initialized()) {
		clulog(LOG_DEBUG, "Services not initialized.\n");
		return;
	}

	eval_groups(0, nodeID, nodeStatus);
}


/**
  This updates our local membership view and handles whether or not we
  should exit, as well as determines node transitions (thus, calling
  node_event()).
 
  @see				node_event
  @return			0
 */
int
membership_update(chandle_t *clu)
{
	cluster_member_list_t *new_ml, *node_delta, *old_membership;
	int		x;
	int		me = 0;

	if (!rg_quorate())
		return 0;

	clulog(LOG_INFO, "Magma Event: Membership Change\n");

	old_membership = member_list();
	new_ml = get_member_list(clu->c_cluster);
	member_list_update(new_ml);

	clulog(LOG_DEBUG, "I am node #%lld\n", my_id());

	/*
	 * Handle nodes lost.  Do our local node event first.
	 */
	node_delta = memb_lost(old_membership, new_ml);

	me = memb_online(node_delta, my_id());
	if (me) {
		/* Should not happen */
		clulog(LOG_INFO, "State change: LOCAL OFFLINE\n");
		free_member_list(node_delta);
		node_event(1, my_id(), 0);
		/* NOTREACHED */
	}

	for (x=0; node_delta && x < node_delta->cml_count; x++) {

		clulog(LOG_INFO, "State change: %s DOWN\n",
		       node_delta->cml_members[x].cn_name);
		/* Don't bother evaluating anything resource groups are
		   locked.  This is just a performance thing */
		if (!rg_locked()) {
			node_event_q(0, node_delta->cml_members[x].cn_nodeid,
			     		0);
		} else {
			clulog(LOG_NOTICE, "Not taking action - services"
			       " locked\n");
		}
	}

	/* Free nodes */
	free_member_list(node_delta);

	/*
	 * Handle nodes gained.  Do our local node event first.
	 */
	node_delta = memb_gained(old_membership, new_ml);

	me = memb_online(node_delta, my_id());
	if (me) {
		clulog(LOG_INFO, "State change: Local UP\n");
		node_event_q(1, my_id(), 1);
	}

	for (x=0; node_delta && x < node_delta->cml_count; x++) {

		if (!memb_online(node_delta,
				 node_delta->cml_members[x].cn_nodeid))
			continue;

		if (node_delta->cml_members[x].cn_nodeid == my_id())
			continue;

		clulog(LOG_INFO, "State change: %s UP\n",
		       node_delta->cml_members[x].cn_name);
		node_event_q(0, node_delta->cml_members[x].cn_nodeid,
			     1);
	}

	free_member_list(node_delta);
	free_member_list(new_ml);

	rg_unlockall(L_SYS);

	return 0;
}


int
lock_commit_cb(char *key, uint64_t viewno, void *data, uint32_t datalen)
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


int
do_lockreq(msgctx_t *ctx, int req)
{
#if 0
	int ret;
	char state;
	cluster_member_list_t *m = member_list();

	state = (req==RG_LOCK)?1:0;
	ret = vf_write(m, VFF_IGN_CONN_ERRORS, "rg_lockdown", &state, 1);
	free_member_list(m);

	if (ret == 0) {
		msg_send_simple(ctx, RG_SUCCESS, 0, 0);
	} else {
		msg_send_simple(ctx, RG_FAIL, 0, 0);
	}
#endif
	return 0;
}


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
dispatch_msg(msgctx_t *ctx, uint64_t nodeid)
{
	int ret = -1;
	char msgbuf[4096];
	generic_msg_hdr	*msg_hdr = (generic_msg_hdr *)msg_hdr;
	SmMessageSt	*msg_sm = (SmMessageSt *)msgbuf;

	/* Peek-a-boo */
	ret = msg_receive(ctx, msg_hdr, sizeof(msgbuf), 10);
	if (ret < sizeof (generic_msg_hdr)) {
		clulog(LOG_ERR, "#37: Error receiving message header\n");
		goto out;
	}

	/* Decode the header */
	swab_generic_msg_hdr(msg_hdr);
	if ((msg_hdr->gh_magic != GENERIC_HDR_MAGIC)) {
		clulog(LOG_ERR,
		       "#38: Invalid magic: Wanted 0x%08x, got 0x%08x\n",
		       GENERIC_HDR_MAGIC, msg_hdr->gh_magic);
		goto out;
	}

	if (msg_hdr->gh_length != ret) {
		clulog(LOG_ERR, "#XX: Read size mismatch: %d %d\n",
		       ret, msg_hdr->gh_length);
		goto out;
	}

	ret = 0;
	switch (msg_hdr->gh_command) {
	case RG_STATUS:
		clulog(LOG_DEBUG, "Sending service states to ctx%p\n",ctx);
		send_rg_states(ctx, msg_hdr->gh_arg1);
		break;

	case RG_LOCK:
		if (rg_quorate())
			do_lockreq(ctx, RG_LOCK);
		msg_close(ctx);
		break;

	case RG_UNLOCK:
		if (rg_quorate())
			do_lockreq(ctx, RG_UNLOCK);
		break;

	case RG_QUERY_LOCK:
		if (rg_quorate()) {
			ret = (rg_locked() & L_USER) ? RG_LOCK : RG_UNLOCK;
			msg_send_simple(ctx, ret, 0, 0);
		}
		break;


	case RG_ACTION_REQUEST:

		if (ret != sizeof(msg_sm)) {
			clulog(LOG_ERR,
			       "#39: Error receiving entire request\n");
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

			if (msg_send(ctx, msg_sm, sizeof (SmMessageSt)) !=
		    	    sizeof (SmMessageSt))
				clulog(LOG_ERR, "#40: Error replying to "
				       "action request.\n");
			ret = -1;
			goto out;
		}

		/* Queue request */
		rt_enqueue_request(msg_sm->sm_data.d_svcName,
		  		   msg_sm->sm_data.d_action,
		  		   ctx, 0, msg_sm->sm_data.d_svcOwner, 0, 0);
		return 0;

	case RG_EXITING:

		clulog(LOG_NOTICE, "Member %d is now offline\n", (int)nodeid);

		node_event(0, nodeid, 0);
		break;

	default:
		clulog(LOG_DEBUG, "unhandled message request %d\n",
		       msg_hdr->gh_command);
		break;
	}

out:
	msg_close(ctx);
	return ret;
}

/**
  Grab an event off of the designated context

  @param fd		File descriptor to check
  @return		Event
 */
int
handle_cluster_event(chandle_t *clu, msgctx_t *ctx)
{
	int ret;
	
	ret = msg_wait(ctx, 0);

	switch(ret) {
	case M_PORTOPENED:
	case M_PORTCLOSED:
		/* Might want to handle powerclosed like membership change */
	case M_NONE:
		clulog(LOG_DEBUG, "NULL cluster message\n");
		break;
	case M_OPEN:
	case M_OPEN_ACK:
	case M_CLOSE:
		clulog(LOG_DEBUG, "I should NOT get here: %d\n",
		       ret);
		break;
	case M_STATECHANGE:
		clulog(LOG_DEBUG, "Membership Change Event\n");
		if (rg_quorate() && running) {
			rg_unlockall(L_SYS);
			membership_update(clu);
		}
		break;
		rg_set_quorate();
		rg_unlockall(L_SYS);
		rg_unlockall(L_USER);
		clulog(LOG_NOTICE, "Quorum Achieved\n");
		membership_update(clu);
		break;
	case 999:
		clulog(LOG_EMERG, "#1: Quorum Dissolved\n");
		rg_set_inquorate();
		member_list_update(NULL);		/* Clear member list */
		rg_lockall(L_SYS);
		rg_doall(RG_INIT, 1, "Emergency stop of %s");
		rg_set_uninitialized();
		break;
	case M_TRY_SHUTDOWN:
		clulog(LOG_WARNING, "#67: Shutting down uncleanly\n");
		rg_set_inquorate();
		rg_doall(RG_INIT, 1, "Emergency stop of %s");
#if defined(LIBCMAN_VERSION) && LIBCMAN_VERSION >= 2
		/* cman_replyto_shutdown() */
#endif
		exit(0);
	}

	return ret;
}


void dump_threads(void);

int
event_loop(chandle_t *clu)
{
	int n, max, ret;
	fd_set rfds;
	msgctx_t newctx;
	struct timeval tv;
	int nodeid;
	msgctx_t *localctx = clu->local_ctx;
	msgctx_t *clusterctx = clu->cluster_ctx;

	tv.tv_sec = 10;
	tv.tv_usec = 0;

	if (signalled) {
		signalled = 0;
		/*
		malloc_stats();
		dump_threads();
		 */
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
			handle_cluster_event(clu, clusterctx);
			continue;
		}

		if (!msg_fd_isset(localctx, &rfds)) {
			continue;
		}

		ret = msg_accept(localctx, &newctx);

		if (ret == -1)
			continue;

		if (rg_quorate()) {
			/* Handle message */
			/* When request completes, the fd is closed */
			nodeid = msg_get_nodeid(&newctx);
			dispatch_msg(&newctx, nodeid);
			continue;
		}
			
		if (!rg_initialized()) {
			msg_close(&newctx);
			continue;
		}

		if (!rg_quorate()) {
			printf("Dropping connect: NO QUORUM\n");
			msg_close(&newctx);
		}
	}

	if (need_reconfigure || check_config_update()) {
		need_reconfigure = 0;
		configure_logging(-1);
		init_resource_groups(1);
		return 0;
	}

	/* Did we receive a SIGTERM? */
	if (n < 0)
		return 0;

	/* No new messages.  Drop in the status check requests.  */
	if (n == 0) {
		do_status_checks();
		return 0;
	}

	return 0;
}


void
flag_shutdown(int sig)
{
	shutdown_pending = 1;
}


void
graceful_exit(int sig)
{
	running = 0;
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
	rg_lockall(L_SYS);
	rg_doall(RG_STOP_EXITING, 1, NULL);
	//vf_shutdown();
	kill_resource_groups();
	member_list_update(NULL);
	notify_exiting();
}



void
statedump(int sig)
{
	signalled++;
}


void malloc_dump_table(size_t, size_t);


/*
 * Configure logging based on data in cluster.conf
 */
int
configure_logging(int ccsfd)
{
	char *v;
	char internal = 0;

	if (ccsfd == -1) {
		internal = 1;
		ccsfd = ccs_connect();
		if (ccsfd == -1)
			return -1;
	}

	if (ccs_get(ccsfd, "/cluster/rm/@log_facility", &v) == 0) {
		clu_set_facility(v);
		free(v);
	}

	if (ccs_get(ccsfd, "/cluster/rm/@log_level", &v) == 0) {
		clu_set_loglevel(atoi(v));
		free(v);
	}

	if (internal)
		ccs_disconnect(ccsfd);

	return 0;
}


void
clu_initialize(chandle_t *clu)
{
	cman_node_t me;

	clu->c_cluster = cman_init(NULL);
	if (!clu->c_cluster) {
		clulog(LOG_NOTICE, "Waiting for CMAN to start\n");

		while (!(clu->c_cluster = cman_init(NULL))) {
			sleep(1);
		}
	}

        if (!cman_is_quorate(clu->c_cluster)) {
		/*
		   There are two ways to do this; this happens to be the simpler
		   of the two.  The other method is to join with a NULL group 
		   and log in -- this will cause the plugin to not select any
		   node group (if any exist).
		 */
		clulog(LOG_NOTICE, "Waiting for quorum to form\n");

		while (cman_is_quorate(clu->c_cluster) == 0) {
			sleep(1);
		}
		clulog(LOG_NOTICE, "Quorum formed, starting\n");
	}

        cman_get_node(clu->c_cluster, CMAN_NODEID_US, &me);
        clu->c_nodeid = me.cn_nodeid;
}


void
set_nonblock(int fd)
{
       int flags;

       flags = fcntl(fd, F_GETFL, 0);
       fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}


int
main(int argc, char **argv)
{
	int rv;
	char foreground = 0;
	int quorate;
	int listen_fds[2], listeners;
	int myNodeID;
	chandle_t clu;

	while ((rv = getopt(argc, argv, "fd")) != EOF) {
		switch (rv) {
		case 'd':
			debug = 1;
			break;
		case 'f':
			foreground = 1;
		default:
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
		if (!debug && !watchdog_init())
			clulog(LOG_NOTICE, "Failed to start watchdog\n");
	}

	clu_initialize(&clu);
	set_my_id(clu.c_nodeid);

	/*
	   We know we're quorate.  At this point, we need to
	   read the resource group trees from ccsd.
	 */
	configure_logging(-1);
	clulog(LOG_NOTICE, "Resource Group Manager Starting\n");

	if (init_resource_groups(0) != 0) {
		clulog(LOG_CRIT, "#8: Couldn't initialize services\n");
		return -1;
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

	if (msg_init(&clu) < 0) {
		clulog(LOG_CRIT, "#10: Couldn't set up message system\n");
		return -1;
	}

	rg_set_quorate();
	set_my_id(clu.c_nodeid);

	/*
	   Initialize the VF stuff.
	 */
#if 0
	if (vf_init(clu.c_nodeid, RG_VF_PORT, NULL, NULL) != 0) {
		clulog(LOG_CRIT, "#11: Couldn't set up VF listen socket\n");
		return -1;
	}

	vf_key_init("rg_lockdown", 10, NULL, lock_commit_cb);
#endif

	/*
	   Get an initial membership view.
	 */
	membership_update(&clu);

	/*
	   Do everything useful
	 */
	while (running)
		event_loop(&clu);

	clulog(LOG_NOTICE, "Shutting down\n");
	cleanup(&clu);
	clulog(LOG_NOTICE, "Shutdown complete, exiting\n");
	
	/*malloc_dump_table(); */ /* Only works if alloc.c us used */
	/*malloc_stats();*/

	exit(0);
}
