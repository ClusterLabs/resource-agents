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
#include <magma.h>
#include <magmamsg.h>
#include <platform.h>
#include <ccs.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <rg_locks.h>
#include <resgroup.h>
#include <clulog.h>
#include <msgsimple.h>
#include <vf.h>
#include <rg_queue.h>
#include <malloc.h>


int daemon_init(char *);
int init_resource_groups(void);
void kill_resource_groups(void);
void set_my_id(uint64_t);
int eval_groups(int, uint64_t, int);
void graceful_exit(int);
void flag_shutdown(int sig);
void hard_exit(void);
int send_rg_states(int);

int shutdown_pending = 0, running = 1;

#define request_failback(a) 0


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
	int fd;

	fd = msg_open(nodeid, RG_PORT, 0, 5);
	msg_send_simple(fd, RG_EXITING, 0, 0);
	msg_close(fd);

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

		partner = membership->cml_members[x].cm_id;

		if (partner == my_id() ||
		    membership->cml_members[x].cm_state != STATE_UP)
			continue;

		send_exit_msg(partner);
	}

	cml_free(membership);

	return 0;
}


int
request_failbacks(void)
{
	int x;
	uint64_t partner;
	cluster_member_list_t *membership;

	membership = member_list();

	for (x = 0; x < membership->cml_count; x++) {

		partner = membership->cml_members[x].cm_id;

		if (partner == my_id() ||
		    membership->cml_members[x].cm_state != STATE_UP)
			continue;

		if (request_failback(partner) != 0) {
			clulog(LOG_ERR, "Unable to inform partner "
			       "to start failback\n");
		}
	}

	cml_free(membership);

	return 0;
}


void
reconfigure(int sig)
{
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
	if (local) {

		/* Local Node Event */
		if (nodeStatus == STATE_DOWN)
			hard_exit();

		if (!rg_initialized()) {
			if (init_resource_groups() != 0) {
				clulog(LOG_ERR,
				       "Cannot initialize services\n");
				hard_exit();
			}
		}

		if (shutdown_pending) {
			clulog(LOG_NOTICE, "Processing delayed exit signal\n");
			graceful_exit(SIGINT);
		}
		setup_signal(SIGINT, graceful_exit);
		setup_signal(SIGTERM, graceful_exit);
		setup_signal(SIGHUP, reconfigure);

		eval_groups(1, nodeID, STATE_UP);
		return;
	}

	/*
	 * Nothing to do for events from other nodes if we are not ready.
	 */
	if (!rg_initialized()) {
		clulog(LOG_DEBUG, "Resource groups not initialized.\n");
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
membership_update(void)
{
	cluster_member_list_t *new_ml, *node_delta, *old_membership;
	int		x;
	int		me = 0;

	if (!rg_quorate())
		return 0;

	clulog(LOG_INFO, "Magma Event: Membership Change\n");

	old_membership = member_list();
	new_ml = clu_member_list(RG_SERVICE_GROUP);
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
		cml_free(node_delta);
		node_event(1, my_id(), STATE_DOWN);
		/* NOTREACHED */
	}

	for (x=0; node_delta && x < node_delta->cml_count; x++) {

		node_event(0, node_delta->cml_members[x].cm_id,
			   STATE_DOWN);
		clulog(LOG_INFO, "State change: %s DOWN\n",
		       node_delta->cml_members[x].cm_name);
	}

	/* Free nodes */
	cml_free(node_delta);

	/*
	 * Handle nodes gained.  Do our local node event first.
	 */
	node_delta = memb_gained(old_membership, new_ml);

	me = memb_online(node_delta, my_id());
	if (me) {
		clulog(LOG_INFO, "State change: Local UP\n");
		node_event(1, my_id(), STATE_UP);
	}

	for (x=0; node_delta && x < node_delta->cml_count; x++) {

		if (!memb_online(node_delta,
				 node_delta->cml_members[x].cm_id))
			continue;

		if (node_delta->cml_members[x].cm_id == my_id())
			continue;

		node_event(0, node_delta->cml_members[x].cm_id,
			   STATE_UP);
		clulog(LOG_INFO, "State change: %s UP\n",
		       node_delta->cml_members[x].cm_name);
	}

	cml_free(node_delta);
	cml_free(new_ml);

	rg_unlockall();
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
dispatch_msg(int fd, uint64_t nodeid)
{
	int ret;
	generic_msg_hdr	msg_hdr;
	SmMessageSt	msg_sm;

	/* Peek-a-boo */
	ret = msg_peek(fd, &msg_hdr, sizeof(msg_hdr));
	if (ret != sizeof (generic_msg_hdr)) {
		clulog(LOG_ERR, "error receiving message header\n");
		return -1;
	}

	/* Decode the header */
	swab_generic_msg_hdr(&msg_hdr);
	if ((msg_hdr.gh_magic != GENERIC_HDR_MAGIC)) {
		clulog(LOG_ERR, "Invalid magic: Wanted 0x%08x, got 0x%08x\n",
		       GENERIC_HDR_MAGIC, msg_hdr.gh_magic);
		return -1;
	}

	switch (msg_hdr.gh_command) {
	case RG_STATUS:
		clulog(LOG_DEBUG, "Sending resource group states to fd%d\n",fd);
		send_rg_states(fd);
		break;

	case RG_ACTION_REQUEST:

		ret = msg_receive_timeout(fd, &msg_sm, sizeof(msg_sm), 
					  10);
		if (ret != sizeof(msg_sm)) {
			clulog(LOG_ERR, "receiving message data from client "
			       "error: %d\n", ret);
			return -1;
		}

		/* Decode SmMessageSt message */
		swab_SmMessageSt(&msg_sm);

		if (rg_locked()) {
			msg_sm.sm_data.d_ret = -EAGAIN;
			/* Encode before responding... */
			swab_SmMessageSt(&msg_sm);

			if (msg_send(fd, &msg_sm, sizeof (SmMessageSt)) !=
		    	    sizeof (SmMessageSt))
				clulog(LOG_ERR,
				       "Error replying to action request.\n");
		}

		/* Queue request */
		rt_enqueue_request(msg_sm.sm_data.d_svcName,
		  		   msg_sm.sm_data.d_action,
		  		   fd, 0, msg_sm.sm_data.d_svcOwner, 0, 0);
		break;

	case RG_EXITING:

		clulog(LOG_NOTICE, "Member %d is now offline\n", (int)nodeid);

		/* Don't really need to do these */
		msg_receive_timeout(fd, &msg_hdr, sizeof(msg_hdr), 5);
		swab_generic_msg_hdr(&msg_hdr);
		msg_close(fd);

		node_event(0, nodeid, STATE_DOWN);
		break;

	default:
		clulog(LOG_DEBUG, "unhandled message request %d\n",
		       msg_hdr.gh_command);
		break;
	}
	return 0;
}

/**
  Grab a magma event off of the designated file descriptor

  @param fd		File descriptor to check
  @return		Event
 */
int
handle_cluster_event(int fd)
{
	int ret;
	
	ret = clu_get_event(fd);

	switch(ret) {
	case CE_NULL:
		break;
	case CE_SUSPEND:
		rg_lockall();
		break;
	case CE_MEMB_CHANGE:
		clulog(LOG_DEBUG, "Membership Change Event\n");
		if (rg_quorate()) {
			rg_unlockall();
			membership_update();
		}
		break;
	case CE_QUORATE:
		rg_set_quorate();
		rg_unlockall();
		clulog(LOG_NOTICE, "Quorum Achieved\n");
		membership_update();
		break;
	case CE_INQUORATE:
		clulog(LOG_EMERG, "Quorum Dissolved\n");
		rg_set_inquorate();
		member_list_update(NULL);		/* Clear member list */
		rg_lockall();
		rg_stopall();
		rg_wait_threads();
		rg_set_uninitialized();
		break;
	case CE_SHUTDOWN:
		clulog(LOG_WARNING, "Shutting down uncleanly\n");
		rg_set_inquorate();
		rg_stopall();
		rg_wait_threads();
		exit(0);
	}

	return ret;
}


uint64_t
next_node_id(cluster_member_list_t *membership, uint64_t me)
{
	uint64_t low = (uint64_t)(-1);
	uint64_t next = me, curr;
	int x;

	for (x = 0; x < membership->cml_count; x++) {
		curr = membership->cml_members[x].cm_id;
		if (curr < low)
			low = curr;

		if ((curr > me) && ((next == me) || (curr < next)))
			next = curr;
	}

	/* I am highest ID; go to lowest */
	if (next == me)
		next = low;

	return next;
}


int
event_loop(int clusterfd)
{
	int newfd, fd, n, max;
	fd_set rfds;
	struct timeval tv;
	uint64_t nodeid;

	FD_ZERO(&rfds);
	max = msg_fill_fdset(&rfds, MSG_LISTEN, RG_PURPOSE);
	FD_SET(clusterfd, &rfds);
	if (clusterfd > max)
		max = clusterfd;

	tv.tv_sec = 5;
	tv.tv_usec = 0;

	n = select(max + 1, &rfds, NULL, NULL, &tv);

	/* No new messages. */

	if (n < 0) {
		return 0;
	}

	while ((fd = msg_next_fd(&rfds)) != -1) {

		if (fd == clusterfd) {
			handle_cluster_event(clusterfd);
			continue;
		}

		/* One of our listen file descriptors */
		while ((newfd = msg_accept(fd, 1, &nodeid)) != -1) {
			if (rg_quorate()) {
				/* Handle message */
				/* When request completes, the fd is closed */
				dispatch_msg(newfd, nodeid);

			} else if (!rg_initialized()) {
				/* return eagain? */
				msg_close(newfd);
			} else {
				printf("Dropping connect: NO QUORUM\n");
				msg_close(newfd);
			}
		}
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
	rg_lockall();
	rg_stopall();
	rg_wait_threads();
	vf_shutdown();
	exit(1);
}


void
cleanup(int cluster_fd)
{
	rg_lockall();
	rg_stopall();
	rg_wait_threads();
	vf_shutdown();
	kill_resource_groups();
	member_list_update(NULL);
	clu_disconnect(cluster_fd);
	msg_shutdown();
	notify_exiting();
}


void dump_threads(void);

void
statedump(int sig)
{
	dump_threads();
	/*malloc_stats();*/
}


int test_func(int, char**);
int tree_delta_test(int, char**);

void malloc_dump_table(void);


void
wait_for_quorum(void)
{
	int fd, q;

	/* Do NOT log in */
	fd = clu_connect(RG_SERVICE_GROUP, 0);

	q = clu_quorum_status(RG_SERVICE_GROUP);
	if (q & QF_QUORATE) {
		clu_disconnect(fd);
		return;
	}

	/*
	   There are two ways to do this; this happens to be the simpler
	   of the two.  The other method is to join with a NULL group 
	   and log in -- this will cause the plugin to not select any
	   node group (if any exist).
	 */
	clulog(LOG_NOTICE, "Waiting for quorum to form\n");

	while (! (q&QF_QUORATE)) {
		sleep(2);
		q = clu_quorum_status(RG_SERVICE_GROUP);
	}

	clulog(LOG_NOTICE, "Quorum formed; resuming\n");
	clu_disconnect(fd);
}


int
main(int argc, char **argv)
{
	int cluster_fd, rv;
	char debug = 0, foreground = 0;
	int quorate;
	int listen_fds[2], listeners;
	uint64_t myNodeID;

	clu_set_loglevel(LOG_NOTICE);

	if (argc >= 2 && !strcmp(argv[1],"test")) {
		--argc; ++argv;
		return test_func(argc, argv);
	}

	if (argc >= 2 && !strcmp(argv[1],"delta")) {
		--argc; ++argv;
		return tree_delta_test(argc, argv);
	}

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

	if (!foreground && (geteuid() == 0)) 
		daemon_init(argv[0]);

	/*
	   We need quorum before we can read the configuration data from
	   ccsd.
	 */
	wait_for_quorum();

	/*
	   We know we're quorate.  At this point, we need to
	   read the resource group trees from ccsd.
	 */
	if (init_resource_groups() != 0) {
		clulog(LOG_CRIT, "Couldn't initialize resource groups\n");
		return -1;
	}

	/*
	   Connect to the cluster software.
	 */
	cluster_fd = clu_connect(RG_SERVICE_GROUP, 1);
	if (cluster_fd < 0) {
		clu_log_console(1);
		clulog(LOG_ERR, "Couldn't connect to cluster\n");
		return -1;
	}

	setup_signal(SIGINT, flag_shutdown);
	setup_signal(SIGTERM, flag_shutdown);
	setup_signal(SIGUSR1, statedump);
	unblock_signal(SIGCHLD);
	setup_signal(SIGPIPE, SIG_IGN);
	setup_signal(SIGSEGV, segfault);

	if ((listeners = msg_listen(RG_PORT, RG_PURPOSE,
				    listen_fds, 2)) <= 0) {
		clulog(LOG_CRIT, "Couldn't set up listen socket\n");
		return -1;
	}

   	msg_set_purpose(cluster_fd, MSGP_CLUSTER);
	quorate = (clu_quorum_status(RG_SERVICE_GROUP) & QF_QUORATE);
	if (quorate)
		rg_set_quorate();
	else {
		setup_signal(SIGINT, graceful_exit);
		setup_signal(SIGTERM, graceful_exit);
	}

	clulog(LOG_DEBUG, "USRM: Using %s\n", clu_plugin_version());
	clulog(LOG_DEBUG,"USRM: Cluster Status: %s\n",
	       quorate?"Quorate":"Inquorate");

	clu_local_nodeid(RG_SERVICE_GROUP, &myNodeID);
	set_my_id(myNodeID);

	/*
	   Initialize the VF stuff.
	 */
	if (vf_init(myNodeID, RG_VF_PORT, NULL, NULL) != 0) {
		clulog(LOG_CRIT, "Couldn't set up VF listen socket\n");
		return -1;
	}

	/*
	   Get an initial membership view.
	 */
	membership_update();

	/*
	   Do everything useful
	 */
	while (running)
		event_loop(cluster_fd);

	clulog(LOG_NOTICE, "Shutting down\n");
	cleanup(cluster_fd);
	clulog(LOG_NOTICE, "Shutdown complete, exiting\n");
	
	/*malloc_dump_table(); */ /* Only works if alloc.c us used */
	/*malloc_stats();*/

	exit(0);
}
