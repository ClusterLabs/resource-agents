/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/errno.h>
#include <sys/resource.h>

#include "list.h"
#include "cnxman-socket.h"
#include "cnxman-private.h"
#include "cnxman.h"
#include "daemon.h"
#include "config.h"
#include "libcman.h"

#ifndef TRUE
#define TRUE 1
#endif

/* Barrier name for membership transitions. %d is the cluster generation number
 */
#define MEMBERSHIP_BARRIER_NAME	"TRANSITION.%d"

/* Variables also used by connection manager */
struct list cluster_members_list;
pthread_mutex_t cluster_members_lock;
int cluster_members;		/* Number of ACTIVE members, not a count of
				 * nodes in the list */
int we_are_a_cluster_member;
int cluster_is_quorate;
int quit_threads;
pthread_t membership_pthread;
pthread_mutex_t membership_task_lock;
struct cluster_node *us;

static pthread_t hello_pthread;
static pthread_mutex_t hello_task_lock;

static cman_handle_t cman_handle;


/* Variables that belong to the connection manager */
//extern struct completion member_thread_comp;
extern struct cluster_node *quorum_device;
extern unsigned short two_node;
extern char cluster_name[];
extern unsigned int config_version;
extern unsigned int address_length;

static char scratchbuf[MAX_CLUSTER_MESSAGE + 100];

/* Our node name, usually system_utsname.nodename, but can be overridden */
char nodename[MAX_CLUSTER_MEMBER_NAME_LEN + 1];

/* Node ID that we want. defaults of zero means
 *  it will be allocated by the cluster join mechanism
 */
int wanted_nodeid;

static pthread_mutex_t members_by_nodeid_lock;
static int sizeof_members_array;	/* Can dynamically increase (malloc
					 * permitting) */
static struct cluster_node **members_by_nodeid;

#define MEMBER_INCREMENT_SIZE 10

static int votes = 1;		/* Votes this node has */
static int expected_votes = 1;	/* Total expected votes in the cluster */
static unsigned int quorum;	/* Quorum, fewer votes than this and we stop
				 * work */
static int leavereason;		/* Saved for the duration of a state transition */
static int transitionreason;	/* Reason this transition was initiated */
static unsigned int highest_nodeid;	/* Highest node ID known to the cluster */
static unsigned long join_time;	/* The time that we got our JOIN-ACK */
static unsigned long start_time; /* The time that we were started */
static int joinconf_count;	/* Number of JOINCONF messages we have sent to
				 * a new node */
static unsigned long wake_flags;/* Reason we were woken */

/* Flags in above */
#define WAKE_FLAG_DEADNODE    1
#define WAKE_FLAG_TRANSTIMER  2

/* The time the transition finished */
static unsigned long transition_end_time;

/* A list of nodes that cnxman tells us are dead. I hope this never has more
 * than one element in it but I can't take that chance. only non-static so it
 * can be initialised in module_load. */
struct list new_dead_node_list;
pthread_mutex_t new_dead_node_lock;

static int do_membership_packet(struct msghdr *msg, char *buf, int len);
static int do_process_joinreq(struct msghdr *msg, char *buf, int len);
static int do_process_joinack(struct msghdr *msg, char *buf, int len);
static int do_process_joinconf(struct msghdr *msg, char *buf, int len);
static int do_process_leave(struct msghdr *msg, char *buf, int len);
static int do_process_hello(struct msghdr *msg, char *buf, int len);
static int do_process_kill(struct msghdr *msg, char *buf, int len);
static int do_process_reconfig(struct msghdr *msg, char *buf, int len);
static int do_process_starttrans(struct msghdr *msg, char *buf, int len);
static int do_process_nodepthread_mutex_lock(struct msghdr *msg, char *buf, int len);
static int do_process_masterview(struct msghdr *msg, char *buf, int len);
static int do_process_endtrans(struct msghdr *msg, char *buf, int len);
static int do_process_viewack(struct msghdr *msg, char *buf, int len);
static int do_process_startack(struct msghdr *msg, char *buf, int len);
static int do_process_newcluster(struct msghdr *msg, char *buf, int len);
static int do_process_nominate(struct msghdr *msg, char *buf, int len);
static int send_cluster_view(unsigned char cmd, struct sockaddr_cl *saddr,
			     unsigned int flags, unsigned int flags2);
static int send_joinreq(struct sockaddr_cl *addr, int addr_len);
static int send_startack(struct sockaddr_cl *addr, int addr_len);
static int send_hello(void);
static int send_master_hello(void);
static int send_newcluster(void);
static int end_transition(void);
static void check_for_dead_nodes(void);
static void confirm_joiner(void);
static void reset_hello_time(void);
static int add_us(void);
static int send_joinconf(void);
static int init_membership_services(void);
static int elect_master(struct cluster_node **, int disallow_node);
static void join_or_form_cluster(void);
static int do_timer_wakeup(void);
static int start_transition(unsigned char reason, struct cluster_node *node);
static uint32_t low32_of_ip(void);
static void remove_joiner(int tell_wait);
static int do_timer_wakeup();
int send_leave(unsigned char);
int send_reconfigure(int, unsigned int);

/* May put more granularity into this sometime */
long gettime()
{
	return time(NULL);
}

#ifdef DEBUG
static char *msgname(int msg);
static int debug_senddata(cman_handle_t handle, void *buf, int len, int flags, uint8_t port, int nodeid)
{
	P_MEMB("%ld: sending %s, len=%d\n", gettime(), msgname(((char *) buf)[0]), len);
	return cman_send_data(handle, buf, len, flags, port, nodeid);
}

#define cman_send_data debug_senddata
#endif

/* State of the node */
enum { STARTING, NEWCLUSTER, JOINING, JOINWAIT, JOINACK, TRANSITION,
	    TRANSITION_COMPLETE, MEMBER, REJECTED, LEFT_CLUSTER, MASTER
} node_state = LEFT_CLUSTER;

/* Sub-state when we are MASTER */
static enum { MASTER_START, MASTER_COLLECT, MASTER_CONFIRM,
	    MASTER_COMPLETE } master_state;

/* Number of responses collected while a master controlling a state transition */
static int responses_collected;
static int responses_expected;

/* Current cluster generation number */
int cluster_generation = 1;

/* When another node initiates a transtion then store it's pointer in here so
 * we can check for other nodes trying to spoof us */
struct cluster_node *master_node = NULL;

/* Struct the node wanting to join us */
static struct cluster_node *joining_node = NULL;
static int joining_temp_nodeid;

/* Last time a HELLO message was sent */
unsigned long last_hello;

/* When we got our JOINWAIT or NEWCLUSTER */
unsigned long joinwait_time;

/* Number of times a transition has restarted when we were master */
int transition_restarts;

/* Variables used by the master to collect cluster status during a transition */
static int agreeing_nodes;
static int dissenting_nodes;
#define OPINION_AGREE    1
#define OPINION_DISAGREE 2

static struct timeval transition_timer = {0,0};

static void set_transition_timer(int secs)
{
	transition_timer.tv_sec = secs;
	transition_timer.tv_usec = 0;
}

/* None of our threads is CPU intensive, but if they don't run when they are supposed
   to, the node can get kicked out of the cluster.
*/
void cman_set_realtime()
{
#if 0 // Until debugged!
	struct sched_param s;

	s.sched_priority = 1;
	if (sched_setscheduler(0, SCHED_FIFO, &s))
		log_msg(LOG_WARNING, "Cannot set priority: %s\n", strerror(errno));
#endif
}

/* Called when the transition timer has expired, meaning we sent a transition
 * message that was not ACKed */
static void trans_timer_expired(void)
{
        P_MEMB("Transition timer fired %ld\n", gettime());

        wake_flags |= WAKE_FLAG_TRANSTIMER;
	pthread_kill(membership_pthread, SIGUSR1);
}

/* Set node id of a node, also add it to the members array and expand the array
 * if necessary */
static void set_nodeid(struct cluster_node *node, int nodeid)
{
	if (!nodeid)
		return;

	node->node_id = nodeid;
	if (nodeid >= sizeof_members_array) {
		int new_size = sizeof_members_array + MEMBER_INCREMENT_SIZE;
		struct cluster_node **new_array;

		if (new_size < nodeid)
			new_size = nodeid + MEMBER_INCREMENT_SIZE;

		new_array = malloc((new_size) * sizeof (struct cluster_node *));
		if (new_array) {
			pthread_mutex_lock(&members_by_nodeid_lock);
			memcpy(new_array, members_by_nodeid,
			       sizeof_members_array *
			       sizeof (struct cluster_node *));
			memset(&new_array[sizeof_members_array], 0,
			       (new_size - sizeof_members_array) *
			       sizeof (struct cluster_node *));
			free(members_by_nodeid);

			members_by_nodeid = new_array;
			sizeof_members_array = new_size;
			pthread_mutex_unlock(&members_by_nodeid_lock);
		}
		else {
			log_msg(LOG_CRIT, "No memory for more nodes");
			exit(2);
		}
	}

	/* The old node may be a failed joiner, in which case we can overwrite it with
	   the new node */
	if (members_by_nodeid[nodeid] &&
	    members_by_nodeid[nodeid]->state == NODESTATE_JOINING) {
		struct cluster_node *node;

		P_MEMB("Removing failed joining node %s (%d)\n",
		       members_by_nodeid[nodeid]->name, members_by_nodeid[nodeid]->node_id);

		pthread_mutex_lock(&cluster_members_lock);
		list_del(&members_by_nodeid[nodeid]->list);
		pthread_mutex_unlock(&cluster_members_lock);

		node = members_by_nodeid[nodeid];
		pthread_mutex_lock(&members_by_nodeid_lock);
		members_by_nodeid[nodeid] = NULL;
		pthread_mutex_unlock(&members_by_nodeid_lock);
		free(node);
	}

	if (members_by_nodeid[nodeid] &&
	    members_by_nodeid[nodeid] != node) {
		log_msg(LOG_CRIT, "Attempt to re-add node with id %d\n", nodeid);
		log_msg(LOG_CRIT, "existing node is %s\n", members_by_nodeid[nodeid]->name);
		log_msg(LOG_CRIT, "new node is %s\n", node->name);
		exit(2);
	}

	pthread_mutex_lock(&members_by_nodeid_lock);
	members_by_nodeid[nodeid] = node;
	pthread_mutex_unlock(&members_by_nodeid_lock);
}

static void *hello_thread_fn(void *unused)
{
	cman_set_realtime();

	while (node_state != REJECTED && node_state != LEFT_CLUSTER &&
	       quit_threads == 0) {

		/* Scan the nodes list for dead nodes */
		if (node_state == MEMBER)
			check_for_dead_nodes();

		sleep(cman_config.hello_timer);

		if (node_state != REJECTED && node_state != LEFT_CLUSTER)
			send_hello();
	}

	pthread_mutex_lock(&hello_task_lock);
	hello_pthread = (pthread_t)NULL;
	pthread_mutex_unlock(&hello_task_lock);
	P_MEMB("heartbeat closing down\n");
	return (void *)0;
}

static void process_dead_node(void)
{
	struct list *nodelist, *tmp;
	struct cl_new_dead_node *deadnode;

	wake_flags &= ~WAKE_FLAG_DEADNODE;

	pthread_mutex_lock(&new_dead_node_lock);
	list_iterate_safe(nodelist, tmp, &new_dead_node_list) {
		deadnode = list_item(nodelist,
				     struct cl_new_dead_node);

		list_del(&deadnode->list);
		if (deadnode->node->state == NODESTATE_MEMBER) {
			pthread_mutex_unlock(&new_dead_node_lock);
			a_node_just_died(deadnode->node, 0);
			pthread_mutex_lock(&new_dead_node_lock);
		}
		free(deadnode);
	}
	pthread_mutex_unlock(&new_dead_node_lock);
}

/* This is the membership "daemon". A client of cnxman (but symbiotic with it)
 * that keeps track of and controls cluster membership. */
static void *membership_thread_fn(void *unused)
{
	sigset_t ss;

	/* Open the socket */
	if (init_membership_services())
		return (void *)-1;

	sigemptyset(&ss);
	sigaddset(&ss, SIGUSR1);

	pthread_sigmask(SIG_UNBLOCK, &ss, NULL);

	/* Set signal mask for pselect */
	sigfillset(&ss);
        sigdelset(&ss, SIGUSR1);

	cman_set_realtime();

	add_us();
	joining_node = us;

	/* Do joining stuff */
	join_or_form_cluster();

	transition_end_time = gettime();

	/* Main loop */
	while (node_state != REJECTED && node_state != LEFT_CLUSTER && !quit_threads) {
		struct timeval tv;
		struct timeval *timer;
		fd_set fds;
		int status = 1;

		FD_ZERO(&fds);
		FD_SET(cman_get_fd(cman_handle), &fds);
		tv.tv_sec = 1;
		tv.tv_usec = 0;

		if (node_state == JOINACK ||
		    node_state == JOINWAIT) {
			timer = &tv;
		}
		else {
			if (transition_timer.tv_sec == 0 &&
			    transition_timer.tv_usec == 0)
				timer = NULL;
			else
				timer = &transition_timer;
		}

		if (!quit_threads && !wake_flags) {
			if (timer) {
				P_MEMB("select with timer %ld/%ld\n", timer->tv_sec, timer->tv_usec);
			}
			else {
				P_MEMB("select with no timer\n");
			}

			status = select(FD_SETSIZE, &fds, NULL, NULL, timer);
		}

		/* Are we being shut down? */
		if (node_state == LEFT_CLUSTER || quit_threads)
			break;

		/* Were we woken by a dead node passed down from cnxman ? */
		if (wake_flags & WAKE_FLAG_DEADNODE) {
			process_dead_node();
		}

		if (status == 0 &&
		    (node_state == MASTER || node_state == TRANSITION))
			trans_timer_expired();

		cman_dispatch(cman_handle, CMAN_DISPATCH_ALL);

		/* Check this again here, in case nodes die while we're doing stuff */
		if (wake_flags & WAKE_FLAG_DEADNODE) {
			process_dead_node();
		}

		/* Were we woken by the transition timer firing ? */
		if (wake_flags & WAKE_FLAG_TRANSTIMER) {
			wake_flags &= ~WAKE_FLAG_TRANSTIMER;

			switch (do_timer_wakeup()) {
			case -1:
				continue;
			case 0:
				break;
			case +1:
				goto leave_cluster;
			}
		}

		/* Got a JOINACK but no JOIN-CONF, start waiting for HELLO
		 * messages again */
		if (node_state == JOINACK &&
		    time_after(gettime(), join_time + cman_config.join_timeout)) {
			P_MEMB("Waited a long time for a join-conf, going back to JOINWAIT state\n");
			node_state = JOINWAIT;
			joinwait_time = gettime();
		}

		/* Have we had an ACK for our JOINREQ message ? */
		if (node_state == JOINING &&
		    time_after(gettime(), join_time + cman_config.join_timeout)) {

			P_MEMB("didn't get JOINACK, going back to JOINWAIT\n");
			node_state = JOINWAIT;
			joinwait_time = gettime();
		}

		/* Have we been in joinwait for too long... */
		if (node_state == JOINWAIT &&
		    time_after(gettime(),
			       joinwait_time + cman_config.joinwait_timeout)) {
			log_msg(LOG_ERR, "Been in JOINWAIT for too long - giving up\n");
			goto leave_cluster;
		}
	}

      leave_cluster:

	/* Wake up the heartbeat thread so it can exit */
	pthread_mutex_lock(&hello_task_lock);
	if (hello_pthread)
		pthread_cancel(hello_pthread);
	pthread_mutex_unlock(&hello_task_lock);

	P_MEMB("waiting for hello thread to shut down\n");
	pthread_join(hello_pthread, NULL);

	node_state = LEFT_CLUSTER;
	P_MEMB("closing down\n");
	quit_threads = 1;	/* force other thread to exit too */

	send_leave(us->leave_reason);
	cman_end_recv_data(cman_handle);
	cman_finish(cman_handle);

	highest_nodeid = 0;
	joining_node = NULL;
	master_node = NULL;

	pthread_mutex_lock(&membership_task_lock);
	membership_pthread = (pthread_t)NULL;
	pthread_mutex_unlock(&membership_task_lock);
	P_MEMB("the end\n");
	return (void *)0;
}

void stop_membership_thread()
{
	pthread_mutex_lock(&membership_task_lock);

	if (membership_pthread) {

		pthread_kill(membership_pthread, SIGUSR1);
		pthread_mutex_unlock(&membership_task_lock);
		pthread_join(membership_pthread, NULL);
	}
	else
		pthread_mutex_unlock(&membership_task_lock);

}

/* Things to do in the main thread when the transition timer has woken us.
 * Usually this happens when a transition is taking too long and we need to
 * take remedial action.
 *
 * returns: -1 continue; 0 carry on processing +1 leave cluster; */
static int do_timer_wakeup()
{
	P_MEMB("Timer wakeup - checking for dead master node %ld\n", gettime());

	/* Resend JOINCONF if it got lost on the wire */
	if (node_state == MASTER && master_state == MASTER_CONFIRM) {
		set_transition_timer(cman_config.joinconf_timeout);
		if (++joinconf_count < cman_config.max_retries) {
			P_MEMB("Resending JOINCONF\n");
			send_joinconf();
		}
		else {
			P_MEMB("JOINCONF not acked, removing node\n");
			joining_node->state = NODESTATE_DEAD;
			start_transition(TRANS_REMNODE, joining_node);
			remove_joiner(1);
			joining_node = NULL;
		}
		return -1;
	}

	/* A joining node probably died */
	if (cluster_members == 1) {
		end_transition();
		return -1;
	}

	/* See if the master is still there */
	if (node_state == TRANSITION || node_state == TRANSITION_COMPLETE) {

		/* If we are in transition and master_node is NULL then we are
		 * waiting for ENDTRANS after JOIN-CONF */
		if (!master_node) {
			/* Hmmm. master died after sending JOINCONF, we'll have
			 * to die as we are in mid-transition */
			log_msg(LOG_CRIT, "Master died after JOINCONF, we must leave the cluster\n");
			quit_threads = 1;
			return +1;
		}

		/* No messages from the master - see if it's stil there */
		if (master_node->state == NODESTATE_MEMBER) {
			send_master_hello();
			set_transition_timer(cman_config.transition_timeout);
		}

		/* If the master is dead then elect a new one */
		if (master_node->state == NODESTATE_DEAD) {

			struct cluster_node *node;

			P_MEMB("Master node is dead...Election!\n");
			if (elect_master(&node, 0)) {

				/* We are master now, all kneel */
				master_node->leave_reason = CLUSTER_LEAVEFLAG_NORESPONSE;
				start_transition(TRANS_DEADMASTER, master_node);
			}
			else {
				/* Leave the job to someone on more pay */
				master_node = node;
				set_transition_timer(cman_config.transition_timeout);
			}
		}
	}

	/* If we are the master node then restart the transition */
	if (node_state == MASTER) {
		start_transition(TRANS_RESTART, us);
	}

	return 0;
}

static void form_cluster(void)
{
	log_msg(LOG_INFO, "forming a new cluster\n");
	node_state = MEMBER;
	we_are_a_cluster_member = TRUE;
	us->state = NODESTATE_MEMBER;
	if (wanted_nodeid)
		set_nodeid(us, wanted_nodeid);
	else
		set_nodeid(us, 1);
	recalculate_quorum(0);

	send_hello();

	pthread_create(&hello_pthread, NULL, hello_thread_fn, NULL);
}

/* This does the initial JOIN part of the membership process. Actually most of
 * is done in the message processing routines but this is the main loop that
 * controls it. The side-effect of this routine is "node_state" which tells the
 * real main loop (in the kernel thread routine) what to do next */
static void join_or_form_cluster()
{
	start_time = gettime();

	log_msg(LOG_INFO, "Waiting to join or form a Linux-cluster\n");

 restart_joinwait:
	join_time = 0;
	start_time = gettime();
	joinwait_time = gettime();
	last_hello = 0;

	/* Listen for HELLO or NEWCLUSTER messages */
	do {
		fd_set fds;
		struct timeval tv;
		int status;

		FD_ZERO(&fds);
		FD_SET(cman_get_fd(cman_handle), &fds);
		tv.tv_sec = cman_config.joinwait_timeout / 5;
		tv.tv_usec = 0;

		status = select(FD_SETSIZE, &fds, NULL, NULL, &tv);

		cman_dispatch(cman_handle, CMAN_DISPATCH_ALL);

		if (quit_threads)
			node_state = LEFT_CLUSTER;

	}
	while (time_before(gettime(), start_time + cman_config.joinwait_timeout) &&
	       node_state == STARTING);

	if (node_state == STARTING) {
		start_time = gettime();
		joinwait_time = gettime();
		node_state = NEWCLUSTER;
	}

	send_newcluster();
        /* If we didn't hear any HELLO messages then start sending NEWCLUSTER messages */
	while (time_before(gettime(), start_time + cman_config.newcluster_timeout ) &&
	       node_state == NEWCLUSTER) {
		fd_set fds;
		struct timeval tv;
		int status;

		FD_ZERO(&fds);
		FD_SET(cman_get_fd(cman_handle), &fds);
		tv.tv_sec = cman_config.joinwait_timeout / 5;
		tv.tv_usec = 0;

		status = select(FD_SETSIZE, &fds, NULL, NULL, &tv);
		P_MEMB("select returned %d\n", status);

		if (status == 0)
			send_newcluster();
		else
			cman_dispatch(cman_handle, CMAN_DISPATCH_ALL);

		/* Did we get a lower "NEWCLUSTER" message ? */
		if (node_state == STARTING) {
			P_MEMB("NEWCLUSTER: restarting joinwait\n");
			goto restart_joinwait;
		}

		if (quit_threads)
			node_state = LEFT_CLUSTER;
	}


        /* If we didn't hear any HELLO messages then form a new cluster */
	if (node_state == NEWCLUSTER) {
		form_cluster();
	}
	else
		last_hello = gettime();

}

int start_membership_services()
{
	wake_flags = 0L;

	/* Start the thread */
	return pthread_create(&membership_pthread, NULL, membership_thread_fn, NULL);
}

static void cman_datacallback(cman_handle_t handle, void *private,
			      char *buf, int len, uint8_t port, int nodeid)
{
	struct msghdr msg;
	struct sockaddr_cl saddr;

	msg.msg_name = &saddr;
	msg.msg_namelen = sizeof(saddr);
	saddr.scl_nodeid = nodeid;
	saddr.scl_port = port;

	P_MEMB("data callback from node %d, port %d, len = %d\n", nodeid, port, len);
	do_membership_packet(&msg, buf, len);
}


static int init_membership_services()
{
	pthread_mutex_init(&hello_task_lock, NULL);

	cman_handle = cman_init(NULL);
	if (!cman_handle) {
		log_msg(LOG_CRIT, "Cannot connect to cman\n");
		exit(9);
	}
	cman_start_recv_data(cman_handle, cman_datacallback, CLUSTER_PORT_MEMBERSHIP);

	node_state = STARTING;
	return 0;
}

static int send_joinconf()
{
	struct sockaddr_cl saddr;
	int status;

	if (joining_temp_nodeid == 0) {
		log_msg(LOG_ERR, "Failed to join node '%s'\n",
		       joining_node?joining_node->name:"unknown");
		remove_joiner(0);
		return -1;
        }

	master_state = MASTER_CONFIRM;
	saddr.scl_port = CLUSTER_PORT_MEMBERSHIP;
	saddr.scl_nodeid = joining_temp_nodeid;
	status = send_cluster_view(CLUSTER_MEM_JOINCONF, &saddr,
				   MSG_NOACK, 0);

	if (status < 0) {
		log_msg(LOG_ERR, "Error %d sending JOINCONF\n", status);
        }
	return status;
}

static int send_joinreq(struct sockaddr_cl *addr, int addr_len)
{
	char *msgbuf = scratchbuf;
	struct list *addrlist;
	int ptr = sizeof (struct cl_mem_join_msg);
	unsigned short num_addr = 0;
	struct cluster_node_addr *nodeaddr;
	struct cl_mem_join_msg *msg = (struct cl_mem_join_msg *) msgbuf;

	msg->cmd = CLUSTER_MEM_JOINREQ;
	msg->votes = votes;
	msg->expected_votes = cpu_to_le32(expected_votes);
	msg->nodeid         = cpu_to_le32(wanted_nodeid);
	msg->major_version  = cpu_to_le32(CNXMAN_MAJOR_VERSION);
	msg->minor_version  = cpu_to_le32(CNXMAN_MINOR_VERSION);
	msg->patch_version  = cpu_to_le32(CNXMAN_PATCH_VERSION);
	msg->config_version = cpu_to_le32(config_version);
	msg->addr_len       = cpu_to_le32(address_length);
	strcpy(msg->clustername, cluster_name);

	/* Add our addresses */
	list_iterate(addrlist, &us->addr_list) {
		nodeaddr = list_item(addrlist, struct cluster_node_addr);

		memcpy(msgbuf + ptr, nodeaddr->addr, address_length);
		ptr += address_length;
		num_addr++;
	}
	msg->num_addr = cpu_to_le16(num_addr);

	/* And our name */
	strcpy(msgbuf + ptr, nodename);
	ptr += strlen(nodename) + 1;

	return cman_send_data(cman_handle, msgbuf, ptr, MSG_NOACK, addr->scl_port, addr->scl_nodeid);
}

static int send_startack(struct sockaddr_cl *addr, int addr_len)
{
	struct cl_mem_startack_msg msg;

	msg.cmd = CLUSTER_MEM_STARTACK;
	msg.generation = cpu_to_le32(cluster_generation);

	return cman_send_data(cman_handle, &msg, sizeof(msg), MSG_REPLYEXP, addr->scl_port, addr->scl_nodeid);
}

static int send_newcluster()
{
	char buf[5];
	uint32_t lowip;

	if (node_state != NEWCLUSTER)
		return 0;

	buf[0] = CLUSTER_MEM_NEWCLUSTER;
	lowip = cpu_to_le32(low32_of_ip());
	memcpy(&buf[1], &lowip, sizeof(lowip));

	return cman_send_data(cman_handle, buf, sizeof(uint32_t)+1, MSG_NOACK, 0, 0);
}

static int send_hello()
{
	struct cl_mem_hello_msg hello_msg;
	int status;

	hello_msg.cmd = CLUSTER_MEM_HELLO;
	hello_msg.members = cpu_to_le16(cluster_members);
	hello_msg.flags = cluster_is_quorate ? HELLO_FLAG_QUORATE : 0;
	hello_msg.generation = cpu_to_le32(cluster_generation);

	status = cman_send_data(cman_handle, &hello_msg, sizeof(struct cl_mem_hello_msg), MSG_NOACK | MSG_ALLINT, 0, 0);

	last_hello = gettime();

	return status;
}

/* This is a special HELLO message that requires an ACK. clients in transition
 * send these to the master to check it is still alive. If it does not ACK then
 * cnxman will signal it dead and we can restart the transition */
static int send_master_hello()
{
	struct cl_mem_hello_msg hello_msg;
	int status;

	hello_msg.cmd = CLUSTER_MEM_HELLO;
	hello_msg.members = cpu_to_le16(cluster_members);
	hello_msg.flags = HELLO_FLAG_MASTER |
		          (cluster_is_quorate ? HELLO_FLAG_QUORATE : 0);
	hello_msg.generation = cpu_to_le32(cluster_generation);

	status = cman_send_data(cman_handle, &hello_msg, sizeof(struct cl_mem_hello_msg), 0,
				CLUSTER_PORT_MEMBERSHIP, master_node->node_id);

	last_hello = gettime();

	return status;
}

static int wait_for_completion_barrier(void)
{
	int status;
	char barriername[MAX_BARRIER_NAME_LEN];

	sprintf(barriername, MEMBERSHIP_BARRIER_NAME, cluster_generation);

	/* Make sure we all complete together */
	P_MEMB("Waiting for completion barrier: %d members\n", cluster_members);
	if ((status = cman_barrier_register(cman_handle, barriername, 0, cluster_members)) < 0) {
		log_msg(LOG_ERR, "Error registering barrier: %s\n", strerror(errno));
		return -1;
	}
	cman_barrier_change(cman_handle, barriername, BARRIER_SETATTR_TIMEOUT,
			    cman_config.transition_timeout);
	status = cman_barrier_wait(cman_handle, barriername);
	P_MEMB("Completion barrier reached : status = %d\n", status);
	cman_barrier_delete(cman_handle, barriername);

	P_MEMB("Completion barrier finished\n");
	return status;
}

/* Called at the end of a state transition when we are the master */
static int end_transition()
{
	struct cl_mem_endtrans_msg msg;
	unsigned int total_votes;
	int status;

	/* Cancel the timer */
	set_transition_timer(0);

	confirm_joiner();

	quorum = calculate_quorum(leavereason, leavereason?cluster_members:0, &total_votes);

	msg.cmd = CLUSTER_MEM_ENDTRANS;
	msg.quorum = cpu_to_le32(quorum);
	msg.generation = cpu_to_le32(++cluster_generation);
	msg.total_votes = cpu_to_le32(total_votes);
	if (joining_node && transitionreason == TRANS_NEWNODE) {
		msg.new_node_id = cpu_to_le32(joining_node->node_id);
	}
	else {
		msg.new_node_id = 0;
	}

	status = cman_send_data(cman_handle, &msg, sizeof(msg), 0, CLUSTER_PORT_MEMBERSHIP, 0);

	if (wait_for_completion_barrier() != 0) {
		P_MEMB("Barrier timed out - restart\n");

		/* If a node died while we were waiting then restart transition with ANOTHERREMNODE */
		if (wake_flags & WAKE_FLAG_DEADNODE) {
			remove_joiner(0);
			start_transition(TRANS_RESTART, us);
		}
		return 0;
	}

	joining_temp_nodeid = 0;
	joining_node = NULL;
	purge_temp_nodeids();

	set_quorate(total_votes);

	notify_listeners(NULL, EVENT_REASON_STATECHANGE, 0);
	reset_hello_time();

	/* Tell any waiting barriers that we had a transition */
	check_barrier_returns();

	leavereason = 0;
	node_state = MEMBER;
	transition_end_time = gettime();

	return 0;
}

int send_reconfigure(int param, unsigned int value)
{
	char msgbuf[66];
	struct cl_mem_reconfig_msg *msg =
	    (struct cl_mem_reconfig_msg *) &msgbuf;

	if (param == RECONFIG_PARAM_EXPECTED_VOTES && expected_votes > value)
		expected_votes = value;

	msg->cmd = CLUSTER_MEM_RECONFIG;
	msg->param = param;
	msg->value = cpu_to_le32(value);

	return cman_send_data(cman_handle, &msgbuf, sizeof (*msg), 0, 0, 0);
}

static int send_joinack(struct sockaddr_cl *addr, int addr_len, unsigned char acktype)
{
	struct cl_mem_joinack_msg msg;

	msg.cmd = CLUSTER_MEM_JOINACK;
	msg.acktype = acktype;

	return cman_send_data(cman_handle, &msg, sizeof (msg), MSG_NOACK, addr->scl_port, addr->scl_nodeid);
}

/* Only send a leave message to one node in the cluster so that it can master
 * the state transition, otherwise we get a "thundering herd" of potential
 * masters fighting it out */
int send_leave(unsigned char flags)
{
	char msg[2];
	struct cluster_node *node = NULL;
	int status;

	/* If we are in transition then use the current master */
	if (node_state == TRANSITION) {
		node = master_node;
	}
	if (!node) {
		/* If we are the master or not in transition then pick a node
		 * almost at random */
		struct list *nodelist;

		pthread_mutex_lock(&cluster_members_lock);
		list_iterate(nodelist, &cluster_members_list) {
			node = list_item(nodelist, struct cluster_node);

			if (node->state == NODESTATE_MEMBER && !node->us)
				break;
		}
		pthread_mutex_unlock(&cluster_members_lock);
	}

	/* we are the only member of the cluster - there is no-one to tell */
	if (node && !node->us) {

		P_MEMB("Sending LEAVE to %s\n", node->name);
		msg[0] = CLUSTER_MEM_LEAVE;
		msg[1] = flags;

		/* Direct call into cman because the daemon is probably shutting down */
		status = cl_sendmsg(NULL, 1, node->node_id,
				    MSG_NOACK, msg, 2);
		if (status < 0) {
			P_MEMB("Send leave status = %d\n", status);
			return status;
		}
	}

	/* And exit */
	node_state = LEFT_CLUSTER;
	pthread_kill(membership_pthread, SIGUSR1);
	return 0;
}

int send_kill(int nodeid, int needack)
{
	char killmsg;

	killmsg = CLUSTER_MEM_KILL;

	return cman_send_data(cman_handle, &killmsg, 1, needack?0:MSG_NOACK, 0, nodeid);
}

/* Tell the rest of the cluster a node has gone down */
static int send_nodedown(int nodeid, unsigned char reason)
{
	struct cl_mem_nodedown_msg downmsg;

	downmsg.reason = reason;
	downmsg.nodeid = cpu_to_le32(nodeid);
	downmsg.cmd = CLUSTER_MEM_NODEDOWN;

	return cman_send_data(cman_handle, &downmsg, sizeof(downmsg), 0, 0, 0);
}

/* Process a message */
static int do_membership_packet(struct msghdr *msg, char *buf, int len)
{
	int result = -1;
	struct sockaddr_cl *saddr = msg->msg_name;
	struct cluster_node *node;

	node = find_node_by_nodeid(saddr->scl_nodeid);

	P_MEMB("got membership message : %s, from (%d) %s, len = %d\n",
	       msgname(*buf), saddr->scl_nodeid, node ? node->name : "unknown", len);

	switch (*buf) {
	case CLUSTER_MEM_JOINREQ:
		result = do_process_joinreq(msg, buf, len);
		break;

	case CLUSTER_MEM_LEAVE:
		if (we_are_a_cluster_member)
			result = do_process_leave(msg, buf, len);
		break;

	case CLUSTER_MEM_HELLO:
		result = do_process_hello(msg, buf, len);
		break;

	case CLUSTER_MEM_KILL:
		if (we_are_a_cluster_member)
			result = do_process_kill(msg, buf, len);
		break;

	case CLUSTER_MEM_JOINCONF:
		if (node_state == JOINACK) {
			do_process_joinconf(msg, buf, len);
		}
		break;

	case CLUSTER_MEM_CONFACK:
		if (node_state == MASTER && master_state == MASTER_CONFIRM) {
			end_transition();
		}
		break;

	case CLUSTER_MEM_MASTERVIEW:
		if (node_state == TRANSITION)
			do_process_masterview(msg, buf, len);
		break;

	case CLUSTER_MEM_JOINACK:
		if (node_state == JOINING || node_state == JOINWAIT) {
			do_process_joinack(msg, buf, len);
		}
		break;
	case CLUSTER_MEM_RECONFIG:
		if (we_are_a_cluster_member) {
			do_process_reconfig(msg, buf, len);
		}
		break;

	case CLUSTER_MEM_STARTTRANS:
		result = do_process_starttrans(msg, buf, len);
		break;

	case CLUSTER_MEM_NODEDOWN:
		result = do_process_nodepthread_mutex_lock(msg, buf, len);
		break;

	case CLUSTER_MEM_ENDTRANS:
		result = do_process_endtrans(msg, buf, len);
		break;

	case CLUSTER_MEM_VIEWACK:
		if (node_state == MASTER && master_state == MASTER_COLLECT)
			result = do_process_viewack(msg, buf, len);
		break;

	case CLUSTER_MEM_STARTACK:
		if (node_state == MASTER)
			result = do_process_startack(msg, buf, len);
		break;

	case CLUSTER_MEM_NEWCLUSTER:
		result = do_process_newcluster(msg, buf, len);
		break;

	case CLUSTER_MEM_NOMINATE:
		if (node_state != MASTER)
			result = do_process_nominate(msg, buf, len);
		break;

	default:
		log_msg(LOG_ERR, "Unknown membership services message %d received from node %d port %d\n",
		       *buf, saddr->scl_nodeid, saddr->scl_port);
		break;

	}
	return result;
}

/* Returns -ve to reject membership of the cluster 0 to accept membership +ve
 * to ignore request (node already joining) */
static int check_duplicate_node(char *name, struct msghdr *msg, int len)
{
	struct cluster_node *node;
	struct sockaddr_cl *saddr = (struct sockaddr_cl *)msg->msg_name;
	char addr[address_length];
	unsigned int addrlen;

	if (strlen(name) >= MAX_CLUSTER_MEMBER_NAME_LEN)
		return -3;

	/* See if we already have a cluster member with that name... */
	node = find_node_by_name(name);
	if (node && node->state != NODESTATE_DEAD) {

		if (node->state == NODESTATE_JOINING)
			return +1;

		log_msg(LOG_WARNING, "Rejecting cluster membership application from %s - already have a node with that name\n",
		       name);
		return -1;

	}

	/* Need to check the node's address too */
	if (get_addr_from_temp_nodeid(saddr->scl_nodeid, addr, &addrlen) &&
	    (node = find_node_by_addr(addr, addrlen)) &&
	    node->state != NODESTATE_DEAD) {

		if (node->state == NODESTATE_JOINING)
			return +1;

		log_msg(LOG_WARNING, "Rejecting cluster membership application from %s - already have a node with that address\n",
		       name);
		return -1;
	}
	return 0;
}

/* Start the state transition */
static int start_transition(unsigned char reason, struct cluster_node *node)
{
	char *startbuf = scratchbuf;
	struct cl_mem_starttrans_msg *msg =
	    (struct cl_mem_starttrans_msg *) startbuf;

	P_MEMB("Start transition - reason = %d(last reason = %d)\n", reason, transitionreason);

	/* If this is a restart then zero the counters */
	if (reason == TRANS_RESTART || reason == TRANS_NEWMASTER) {
		agreeing_nodes = 0;
		dissenting_nodes = 0;
		responses_collected = 0;

		/* Make sure we restart with the right new node if applicable. */
		if (transitionreason == TRANS_NEWNODE && joining_node)
			node = joining_node;

		/* If we are a new master then try to restart the transition proper */
		if (reason == TRANS_NEWMASTER) {
			reason = transitionreason;
			if (reason == TRANS_NEWNODE) {
				if (joining_node)
					node = joining_node;
				else
					reason = TRANS_NEWMASTER;
			}
		}
	}

	/* If we have timed out too many times then just die */
	if (reason == TRANS_RESTART
	    && ++transition_restarts > cman_config.transition_restarts) {
		log_msg(LOG_ERR, "too many transition restarts - will die\n");
		us->leave_reason = CLUSTER_LEAVEFLAG_INCONSISTENT;
		node_state = LEFT_CLUSTER;
		quit_threads = 1;
		exit(2);
		return 0;
	}
	if (reason != TRANS_RESTART)
		transition_restarts = 0;

	/* Only keep the original state transition reason in the global
	 * variable. */
	if (reason != TRANS_ANOTHERREMNODE && reason != TRANS_NEWMASTER &&
	    reason != TRANS_RESTART && reason != TRANS_DEADMASTER)
		transitionreason = reason;

	if (reason == TRANS_DEADMASTER)
		transitionreason = TRANS_REMNODE;

        /* Save the info of the requesting node */
	if (reason == TRANS_NEWNODE)
		joining_node = node;

	node_state = MASTER;
	master_state = MASTER_START;
	responses_collected = 0;
	responses_expected = cluster_members - 1;

	/* If we are on our own then just do it */
	if (responses_expected == 0) {
		P_MEMB("We are on our own...lonely here\n");
		responses_collected--;
		do_process_startack(NULL, NULL, 0);
	}
	else {
		int ptr = sizeof (struct cl_mem_starttrans_msg);
		struct list *addrlist;
		unsigned short num_addrs = 0;
		int flags = MSG_REPLYEXP;

		/* Send the STARTTRANS message */
		msg->cmd = CLUSTER_MEM_STARTTRANS;
		msg->reason = reason;
		msg->votes = node->votes;
		msg->expected_votes = cpu_to_le32(node->expected_votes);
		msg->generation = cpu_to_le32(++cluster_generation);
		msg->nodeid = cpu_to_le32(node->node_id);
		msg->flags = node->leave_reason;

		if (reason == TRANS_NEWNODE) {
			/* Add the addresses */
			list_iterate(addrlist, &node->addr_list) {
				struct cluster_node_addr *nodeaddr =
				    list_item(addrlist,
					       struct cluster_node_addr);

				memcpy(startbuf + ptr, nodeaddr->addr,
				       address_length);
				ptr += address_length;
				num_addrs++;
			}

			/* And the name */
			strcpy(startbuf + ptr, node->name);
			ptr += strlen(node->name) + 1;
		}

		/* If another node died then we must queue the STARTTRANS
		 * messages so that membershipd can carry on processing the
		 * other replies */
		if (reason == TRANS_ANOTHERREMNODE)
			flags |= MSG_QUEUE;

		msg->num_addrs = cpu_to_le16(num_addrs);
		cman_send_data(cman_handle, msg, ptr, 0,0,0);

		/* Set a timer in case we don't get 'em all back */
		set_transition_timer(cman_config.transition_timeout);
	}
	return 0;
}

/* A node has died - decide what to do */
void a_node_just_died(struct cluster_node *node, int in_cman_main)
{
	/* If we are not in the context of kmembershipd then stick it on the
	 * list and wake it */
	if (in_cman_main) {
		struct cl_new_dead_node *newnode =
		    malloc(sizeof (struct cl_new_dead_node));
		if (!newnode)
			return;
		newnode->node = node;
		pthread_mutex_lock(&new_dead_node_lock);
		list_add(&new_dead_node_list, &newnode->list);
		wake_flags |= WAKE_FLAG_DEADNODE;
		pthread_mutex_unlock(&new_dead_node_lock);

		pthread_kill(membership_pthread, SIGUSR1);
		P_MEMB("Passing dead node %s onto kmembershipd\n", node->name);
		return;
	}

	log_msg(LOG_INFO, "removing node %s from the cluster : %s\n",
	       node->name, leave_string(node->leave_reason));

	/* Remove it */
	pthread_mutex_lock(&cluster_members_lock);
	if (node->state == NODESTATE_MEMBER)
		cluster_members--;
	node->state = NODESTATE_DEAD;
	pthread_mutex_unlock(&cluster_members_lock);

	send_nodedown(node->node_id, node->leave_reason);

	/* If we are in normal operation then become master and initiate a
	 * state-transition */
	if (node_state == MEMBER) {
		start_transition(TRANS_REMNODE, node);
		return;
	}

	/* If we are a slave in transition then see if it's the master that has
	 * failed. If not then ignore it. If it /is/ the master then elect a
	 * new one */
	if (node_state == TRANSITION) {
		if (master_node == node) {
			if (elect_master(&node, 0)) {
				set_transition_timer(0);
				node_state = MASTER;

				master_node->leave_reason = CLUSTER_LEAVEFLAG_NORESPONSE;
				start_transition(TRANS_DEADMASTER, master_node);
			}
			else {
				/* Someone else can be in charge - phew! */
			}
		}
		return;
	}

	/* If we are the master then we need to start the transition all over
	 * again */
	if (node_state == MASTER) {
		/* Cancel timer */
		set_transition_timer(0);

		/* Restart the transition */
		start_transition(TRANS_ANOTHERREMNODE, node);
		transition_restarts = 0;
		return;
	}
}

/*
 * Build up and send a set of messages consisting of the whole cluster view.
 * The first byte is the command (cmd as passed in), the second is a flag byte:
 * bit 0 is set in the first message, bit 1 in the last (NOTE both may be set if
 * this is the only message sent The rest is a set of packed node entries, which
 * are NOT split over packets. */
static int send_cluster_view(unsigned char cmd, struct sockaddr_cl *saddr,
			     unsigned int flags, unsigned int flags2)
{
	int ptr = 2;
	int len;
	int status = 0;
	int last_node_start = 2;
	unsigned char first_packet_flag = 1;
	struct list *nodelist;
	struct list *temp;
	struct cluster_node *node;
	char *message = scratchbuf;

	message[0] = cmd;
	P_MEMB("send_cluster_view, msg=%d\n", cmd);

	pthread_mutex_lock(&cluster_members_lock);
	list_iterate_safe(nodelist, temp, &cluster_members_list) {
		node = list_item(nodelist, struct cluster_node);

		P_MEMB("Node %s (%d), state = %d\n", node->name, node->node_id, node->state);

		if (node->state == NODESTATE_MEMBER || node->state == NODESTATE_DEAD) {
			unsigned int evotes;
			unsigned int node_id;
			unsigned short num_addrs = 0;
			unsigned short num_addrs_le;
			struct list *addrlist;

			last_node_start = ptr;

			message[ptr++] = len = strlen(node->name);
			strcpy(&message[ptr], node->name);
			ptr += len;

			message[ptr++] = node->state;

			/* Count the number of addresses this node has */
			list_iterate(addrlist, &node->addr_list) {
				num_addrs++;
			}

			num_addrs_le = cpu_to_le16(num_addrs);
			memcpy(&message[ptr], &num_addrs_le, sizeof (short));
			ptr += sizeof (short);

			/* Pack em in */
			list_iterate(addrlist, &node->addr_list) {

				struct cluster_node_addr *nodeaddr =
					list_item(addrlist,
						   struct cluster_node_addr);

				memcpy(&message[ptr], nodeaddr->addr,
				       address_length);
				ptr += address_length;
			}

			message[ptr++] = node->votes;

			evotes = cpu_to_le32(node->expected_votes);
			memcpy(&message[ptr], &evotes, sizeof (int));
			ptr += sizeof (int);

			node_id = cpu_to_le32(node->node_id);
			memcpy(&message[ptr], &node_id, sizeof (int));
			ptr += sizeof (int);

			/* If the block is full then send it */
			if (ptr > MAX_CLUSTER_MESSAGE) {
				message[1] = first_packet_flag;

				pthread_mutex_unlock(&cluster_members_lock);
				status = cman_send_data(cman_handle, message, last_node_start,
							flags, 0, saddr?saddr->scl_nodeid:0);

				if (status < 0)
					goto send_fail;

				pthread_mutex_lock(&cluster_members_lock);

				first_packet_flag = 0;
				/* Copy the overflow back to the start of the
				 * buffer for the next send */
				memcpy(&message[2], &message[last_node_start],
				       ptr - last_node_start);
				ptr = ptr - last_node_start + 2;
			}
		}
	}

	pthread_mutex_unlock(&cluster_members_lock);

	message[1] = first_packet_flag | 2;	/* The last may also be first */

	status = cman_send_data(cman_handle, message, ptr,
				flags | flags2, 0, saddr?saddr->scl_nodeid:0);

      send_fail:

	return status;
}

/* Make the JOINING node into a MEMBER */
static void confirm_joiner()
{
	if (joining_node && joining_node->state == NODESTATE_JOINING) {
		pthread_mutex_lock(&cluster_members_lock);
		joining_node->state = NODESTATE_MEMBER;
		cluster_members++;
		pthread_mutex_unlock(&cluster_members_lock);
	}
}

/* Reset HELLO timers for all nodes We do this after a state-transition as we
 * have had HELLOS disabled during the transition and if we don't do this the
 * nodes will go on an uncontrolled culling-spree afterwards */
static void reset_hello_time()
{
	struct list *nodelist;
	struct cluster_node *node;

	pthread_mutex_lock(&cluster_members_lock);
	list_iterate(nodelist, &cluster_members_list) {
		node = list_item(nodelist, struct cluster_node);

		if (node->state == NODESTATE_MEMBER) {
			node->last_hello = gettime();
		}

	}
	pthread_mutex_unlock(&cluster_members_lock);
}

/* Calculate the new quorum and return the value. do *not* set it in here as
 * cnxman calls this to check if a new expected_votes value is valid. It
 * (optionally) returns the total number of votes in the cluster */
int calculate_quorum(int allow_decrease, int max_expected, unsigned int *ret_total_votes)
{
	struct list *nodelist;
	struct cluster_node *node;
	unsigned int total_votes = 0;
	unsigned int highest_expected = 0;
	unsigned int newquorum, q1, q2;

	pthread_mutex_lock(&cluster_members_lock);
	list_iterate(nodelist, &cluster_members_list) {
		node = list_item(nodelist, struct cluster_node);

		if (node->state == NODESTATE_MEMBER) {
			highest_expected =
			    max(highest_expected, node->expected_votes);
			total_votes += node->votes;
		}
	}
	pthread_mutex_unlock(&cluster_members_lock);
	if (quorum_device && quorum_device->state == NODESTATE_MEMBER)
		total_votes += quorum_device->votes;

	if (max_expected > 0)
		highest_expected = max_expected;

	/* This quorum calculation is taken from the OpenVMS Cluster Systems
	 * manual, but, then, you guessed that didn't you */
	q1 = (highest_expected + 2) / 2;
	q2 = (total_votes + 2) / 2;
	newquorum = max(q1, q2);

	/* Normally quorum never decreases but the system administrator can
	 * force it down by setting expected votes to a maximum value */
	if (!allow_decrease)
		newquorum = max(quorum, newquorum);

	/* The special two_node mode allows each of the two nodes to retain
	 * quorum if the other fails.  Only one of the two should live past
	 * fencing (as both nodes try to fence each other in split-brain.) */
	if (two_node)
		newquorum = 1;

	if (ret_total_votes)
		*ret_total_votes = total_votes;
	return newquorum;
}

/* Recalculate cluster quorum, set quorate and notify changes */
void recalculate_quorum(int allow_decrease)
{
	unsigned int total_votes;

	quorum = calculate_quorum(allow_decrease, 0, &total_votes);
	set_quorate(total_votes);
	notify_listeners(NULL, EVENT_REASON_STATECHANGE, 0);
}

/* Add new node address to an existing node */
int add_node_address(struct cluster_node *node, unsigned char *addr, int len)
{
	struct cluster_node_addr *newaddr;

	newaddr = malloc(sizeof (struct cluster_node_addr));
	if (!newaddr)
		return -1;

	memcpy(newaddr->addr, addr, len);
	newaddr->addr_len = len;
	list_add(&node->addr_list, &newaddr->list);

	return 0;
}

static struct cluster_node *add_new_node(char *name, unsigned char votes,
					 unsigned int expected_votes,
					 int node_id, int state)
{
	struct cluster_node *newnode;

	/* Look for a dead node with this name */
	newnode = find_node_by_name(name);

	/* Is it already joining */
	if (newnode && newnode->state == NODESTATE_JOINING)
		return NULL;

	/* Update existing information */
	if (newnode && newnode->state == NODESTATE_DEAD) {
		newnode->last_hello = gettime();
		newnode->votes = votes;
		newnode->expected_votes = expected_votes;
		newnode->state = state;
		newnode->us = 0;
		newnode->leave_reason = 0;
		newnode->last_seq_recv = 0;
		newnode->last_seq_acked = 0;
		newnode->last_seq_sent = 0;
		newnode->incarnation++;
		gettimeofday(&newnode->join_time, NULL);
		/* Don't overwrite the node ID */

		if (state == NODESTATE_MEMBER) {
			pthread_mutex_lock(&cluster_members_lock);
			cluster_members++;
			pthread_mutex_unlock(&cluster_members_lock);
		}

		log_msg(LOG_INFO, "node %s rejoining\n", name);
		return newnode;
	}

	newnode = malloc(sizeof (struct cluster_node));
	if (!newnode)
		goto alloc_err;

	memset(newnode, 0, sizeof (struct cluster_node));
	newnode->name = malloc(strlen(name) + 1);
	if (!newnode->name)
		goto alloc_err1;

	strcpy(newnode->name, name);
	newnode->last_hello = gettime();
	newnode->votes = votes;
	newnode->expected_votes = expected_votes;
	newnode->state = state;
	newnode->node_id = node_id;
	newnode->us = 0;
	newnode->leave_reason = 0;
	newnode->last_seq_recv = 0;
	newnode->last_seq_acked = 0;
	newnode->last_seq_sent = 0;
	newnode->incarnation = 0;
	gettimeofday(&newnode->join_time, NULL);
	list_init(&newnode->addr_list);
	set_nodeid(newnode, node_id);

	/* Add the new node to the list */
	pthread_mutex_lock(&cluster_members_lock);
	list_add(&cluster_members_list, &newnode->list);
	if (state == NODESTATE_MEMBER)
		cluster_members++;
	pthread_mutex_unlock(&cluster_members_lock);

	if (state == NODESTATE_MEMBER)
		log_msg(LOG_INFO, "got node %s\n", name);

	return newnode;

      alloc_err1:
	free(newnode);
      alloc_err:
	send_leave(CLUSTER_LEAVEFLAG_PANIC);

	log_msg(LOG_CRIT, "Cannot allocate memory for new cluster node %s\n", name);

	exit(2);
	return NULL;
}

/* Remove node from a NODEDOWN message */
static struct cluster_node *remove_node(int nodeid, unsigned char reason)
{
	struct cluster_node *node;

	/* It may be a failed joiner */
	if (joining_node && joining_node->node_id == nodeid) {
		remove_joiner(0);
	}

	node = find_node_by_nodeid(nodeid);
	if (node && node->state != NODESTATE_DEAD) {
		log_msg(LOG_INFO, "node %s has been removed from the cluster : %s\n",
		       node->name, leave_string(reason));
		pthread_mutex_lock(&cluster_members_lock);
		node->state = NODESTATE_DEAD;
		cluster_members--;
		pthread_mutex_unlock(&cluster_members_lock);
		node->leave_reason = reason;

		/* If this node is us then go quietly */
		if (node->us) {
			log_msg(LOG_INFO, "killed by NODEDOWN message\n");
			node_state = LEFT_CLUSTER;
			quit_threads = 1;
			exit(2);
		}
	}
	return node;
}

/* Add a node from a STARTTRANS or NOMINATE message */
static void add_node_from_starttrans(struct msghdr *msg, char *buf, int len)
{
	/* Add the new node but don't fill in the ID until the master has
	 * confirmed it */
	struct cl_mem_starttrans_msg *startmsg =
	    (struct cl_mem_starttrans_msg *)buf;
	int ptr = sizeof (struct cl_mem_starttrans_msg);
	int i;
	char *name = buf + ptr + le16_to_cpu(startmsg->num_addrs) * address_length;
	char *nodeaddr = buf + sizeof(struct cl_mem_starttrans_msg);

	/* Remove any old joining node */
	remove_joiner(0);

	joining_node = add_new_node(name, startmsg->votes,
				    le32_to_cpu(startmsg->expected_votes),
				    le32_to_cpu(startmsg->nodeid), NODESTATE_JOINING);

	/* add_new_node returns NULL if the node already exists */
	if (!joining_node)
		joining_node = find_node_by_name(name);

	/* Add the node's addresses */
	if (list_empty(&joining_node->addr_list)) {
		for (i = 0; i < le16_to_cpu(startmsg->num_addrs); i++) {
			add_node_address(joining_node, (unsigned char *)buf + ptr, address_length);
			ptr += address_length;
		}
	}

	/* Make sure we have a temp nodeid for the new node in case we
	   become master */
	joining_temp_nodeid = new_temp_nodeid(nodeaddr,
					      address_length);
}

/* We have been nominated as master for a transition */
static int do_process_nominate(struct msghdr *msg, char *buf, int len)
{
	struct cl_mem_starttrans_msg *startmsg =
	    (struct cl_mem_starttrans_msg *)buf;
	struct cluster_node *node = NULL;

	P_MEMB("nominate reason is %d\n", startmsg->reason);
	remove_joiner(1);

	if (startmsg->reason == TRANS_NEWNODE) {
		add_node_from_starttrans(msg, buf, len);
		node = joining_node;
	}

	/* Start_transition needs some node info */
	if (node == NULL)
		node = us;
	start_transition(startmsg->reason, node);
	return 0;
}

/* Got a STARTACK response from a node */
static int do_process_startack(struct msghdr *msg, char *buf, int len)
{
	if (node_state != MASTER && master_state != MASTER_START) {
		P_MEMB("Got StartACK when not in MASTER_STARTING substate\n");
		return 0;
	}

	/* buf is NULL if we are called directly from start_transition */
	if (buf) {
		struct cl_mem_startack_msg *ackmsg =
			(struct cl_mem_startack_msg *)buf;

		/* Ignore any messages wil old generation numbers in them */
		if (le32_to_cpu(ackmsg->generation) != cluster_generation) {
			P_MEMB("Got old generation START-ACK msg - ignoring\n");
			return 0;
		}
	}

	/* If we have all the responses in then move to the next stage */
	if (++responses_collected == responses_expected) {

		/* Behave a little differently if we are on our own */
		if (cluster_members == 1) {
			if (transitionreason == TRANS_NEWNODE) {
				/* If the cluster is just us then confirm at
				 * once */
				joinconf_count = 0;
				set_transition_timer(cman_config.joinconf_timeout);
				if (send_joinconf() < 0)
					end_transition();
				return 0;
			}
			else {	/* Node leaving the cluster */
				unsigned int total_votes;
				quorum = calculate_quorum(leavereason, leavereason?cluster_members:0, &total_votes);
				set_quorate(total_votes);
				leavereason = 0;
				joining_temp_nodeid = 0;
				node_state = MEMBER;
				notify_listeners(NULL, EVENT_REASON_STATECHANGE, 0);
			}
		}
		else {
			int i;

			master_state = MASTER_COLLECT;
			responses_collected = 0;
			responses_expected = cluster_members - 1;
			P_MEMB("Sending MASTERVIEW: expecting %d responses\n",
			       responses_expected);

			send_cluster_view(CLUSTER_MEM_MASTERVIEW, NULL, 0, MSG_REPLYEXP);

			/* Set a timer in case we don't get 'em all back */
			set_transition_timer(cman_config.transition_timeout);

			/* Clear out the opinions */
			for (i = 1; i <= highest_nodeid; i++) {
				struct cluster_node *node = find_node_by_nodeid(i);
				if (node)
					node->transition_opinion = OPINION_AGREE;
			}
		}
	}
	return 0;
}

/* Got a VIEWACK response from a node */
static int do_process_viewack(struct msghdr *msg, char *reply, int len)
{
	struct sockaddr_cl *saddr = msg->msg_name;
	struct cluster_node *node;

	/* This has been known to happen, but I'm not sure why */
	if (saddr->scl_nodeid < 1)
		return 0;

	node = find_node_by_nodeid(saddr->scl_nodeid);
	if (!node)
		return 0;

	/* Keep a list of agreeing and dissenting nodes */
	if (reply[1] == 1) {
		/* ACK - remote node agrees with me */
		P_MEMB("Node agrees\n");
		node->transition_opinion = OPINION_AGREE;
		agreeing_nodes++;
	}
	else {
		/* Remote node disagrees */
		P_MEMB("Node disagrees\n");
		node->transition_opinion = OPINION_DISAGREE;
		dissenting_nodes++;
	}

	P_MEMB("got %d responses, expected %d\n", responses_collected + 1,
	       responses_expected);

	/* Are all the results in yet ? */
	if (++responses_collected == responses_expected) {
		set_transition_timer(0);

		P_MEMB("The results are in: %d agree, %d dissent\n",
		       agreeing_nodes, dissenting_nodes);

		if (agreeing_nodes > dissenting_nodes) {
			/* Kill dissenting nodes */
			int i;

			for (i = 1; i <= highest_nodeid; i++) {
				node = find_node_by_nodeid(i);
				if (node && node->state == NODESTATE_MEMBER &&
				    node->transition_opinion == OPINION_DISAGREE) {
					node->leave_reason = CLUSTER_LEAVEFLAG_INCONSISTENT;
					send_kill(i, 1);
				}
			}
		}
		else {
			/* We must leave the cluster as we are in a minority,
			 * the rest of them can fight it out amongst
			 * themselves. */
			us->leave_reason = CLUSTER_LEAVEFLAG_INCONSISTENT;
			agreeing_nodes = 0;
			dissenting_nodes = 0;
			node_state = LEFT_CLUSTER;
			quit_threads = 1;
			log_msg(LOG_CRIT, "We are in a minority");
			exit(2);
		}

		/* Reset counters */
		agreeing_nodes = 0;
		dissenting_nodes = 0;

		/* Confirm new node */
		if (transitionreason == TRANS_NEWNODE) {
			set_transition_timer(cman_config.joinconf_timeout);
			joinconf_count = 0;
			if (send_joinconf() >= 0)
				return 0;
			/* if send_joinconf failed then complete the transition here and how */
		}

		master_state = MASTER_COMPLETE;

		end_transition();
	}

	return 0;
}

/* Remove the node from the list if it's a brand-new node,
 * otherwise we end up knowing about a node that no-one
 * else has and transitions get a bit fragile!
 *
 * Optionally tells the joining node to cancel it's join and try
 * again later.
 */
static void remove_joiner(int tell_wait)
{
	if (!joining_node)
		return;

	if (tell_wait) {
		struct sockaddr_cl saddr;

		saddr.scl_nodeid = joining_temp_nodeid;
		saddr.scl_port = CLUSTER_PORT_MEMBERSHIP;

		P_MEMB("Postponing membership of node %s (incarnation=%d)\n",
		       joining_node->name, joining_node->incarnation);
		send_joinack(&saddr, sizeof(saddr),
			     JOINACK_TYPE_WAIT);
	}

	if (joining_node->incarnation == 0) {
		P_MEMB("Removing joining node %s\n", joining_node->name);
		pthread_mutex_lock(&cluster_members_lock);
		if (joining_node->state == NODESTATE_MEMBER)
			cluster_members--;
		list_del(&joining_node->list);
		pthread_mutex_unlock(&cluster_members_lock);

		if (joining_node->node_id)
			members_by_nodeid[joining_node->node_id] = NULL;
		free(joining_node);
	}
	else {
		joining_node->state = NODESTATE_DEAD;
	}
	joining_node = NULL;
	joining_temp_nodeid = 0;
}

/* Got an ENDTRANS message */
static int do_process_endtrans(struct msghdr *msg, char *buf, int len)
{
	struct cl_mem_endtrans_msg *endmsg =
		(struct cl_mem_endtrans_msg *)buf;
	struct sockaddr_cl *saddr = (struct sockaddr_cl *) msg->msg_name;

	/* Someone else's state transition */
	if (node_state != TRANSITION && node_state != JOINACK)
		return 0;

	/* Check we got it from the MASTER node */
	if (master_node && master_node->node_id != saddr->scl_nodeid) {
		log_msg(LOG_ERR, "Got ENDTRANS from a node not the master: master: %d, sender: %d\n",
		       master_node->node_id, saddr->scl_nodeid);
		return 0;
	}

	set_transition_timer(0);

	/* Set our new node id */
	if (endmsg->new_node_id && us->node_id == 0) {
		set_nodeid(us, le32_to_cpu(endmsg->new_node_id));
		P_MEMB("our new node ID is %d\n", us->node_id);
	}

	node_state = TRANSITION_COMPLETE;

	if (endmsg->new_node_id)
		confirm_joiner();
	else
		remove_joiner(0);

	cluster_generation = le32_to_cpu(endmsg->generation);

	if (wait_for_completion_barrier() != 0) {
		P_MEMB("Barrier timed out - restart client(ie do nowt)\n");
		node_state = TRANSITION;
		set_transition_timer(cman_config.transition_timeout);
		return 0;
	}

	quorum = le32_to_cpu(endmsg->quorum);
	set_quorate(le32_to_cpu(endmsg->total_votes));
	highest_nodeid = get_highest_nodeid();

	/* Tell any waiting barriers that we had a transition */
	check_barrier_returns();

	purge_temp_nodeids();

	/* Clear up */
	master_node = NULL;
	joining_node = NULL;
	joining_temp_nodeid = 0;

	node_state = MEMBER;

	/* Notify other listeners that transition has completed */
	notify_listeners(NULL, EVENT_REASON_STATECHANGE, 0);
	reset_hello_time();
	transition_end_time = gettime();

	return 0;
}

/* Turn a STARTTRANS message into NOMINATE and send it to the new master */
static int send_nominate(struct cl_mem_starttrans_msg *startmsg, int msglen,
			 int nodeid)
{
	startmsg->cmd = CLUSTER_MEM_NOMINATE;

	return cman_send_data(cman_handle, startmsg, msglen, 0, 0, nodeid);
}

/* Got a NODEDOWN message */
static int do_process_nodepthread_mutex_lock(struct msghdr *msg, char *buf, int len)
{
	struct cl_mem_nodedown_msg *downmsg =
		(struct cl_mem_nodedown_msg *)buf;

	remove_node(le32_to_cpu(downmsg->nodeid), downmsg->reason);
	return 0;
}

/* Got a STARTTRANS message */
static int do_process_starttrans(struct msghdr *msg, char *buf, int len)
{
	struct cl_mem_starttrans_msg *startmsg =
		(struct cl_mem_starttrans_msg *)buf;
	struct sockaddr_cl *saddr = (struct sockaddr_cl *) msg->msg_name;
	struct cluster_node *node;
	unsigned int newgen = le32_to_cpu(startmsg->generation);

	/* Got a WHAT from WHOM? */
	node = find_node_by_nodeid(saddr->scl_nodeid);
	if (!node || node->state != NODESTATE_MEMBER)
		return 0;

	/* Someone else's state transition */
	if (node_state != MEMBER &&
	    node_state != TRANSITION && node_state != MASTER) {
		P_MEMB("Ignoring STARTTRANS, our node state is %d\n", node_state);
		return 0;
	}

	/* Ignore old generation STARTTRANS messages */
	if ((newgen < cluster_generation) ||
	    (newgen == 0xFFFFFFFF && cluster_generation == 0)) {
		P_MEMB("Ignoring STARTTRANS with old generation number\n");
		return 0;
	}

	P_MEMB("Got starttrans: newgen = %d, oldgen = %d, reason = %d\n",
	       newgen, cluster_generation, startmsg->reason);

	/* Up the generation number */
	cluster_generation = newgen;

	/* If we are also a master then decide between us */
	if (node_state == MASTER) {

		int not_master = 0;

		/* If one node is doing a CHECK and another a "real" transition then prevent
		   the CHECK from being master as it's a waste of time */
		if (transitionreason != startmsg->reason) {
			if (transitionreason == TRANS_CHECK)
				not_master = us->node_id;
			if (startmsg->reason == TRANS_CHECK)
				not_master = saddr->scl_nodeid;
		}

		/* See if we really want the responsibility of being master */
		if (elect_master(&node, not_master)) {

			/* I reluctantly accept this position of responsibility
			 */
			P_MEMB("I elected myself master\n");

			/* start_transition will re-establish this */
			set_transition_timer(0);

			start_transition(TRANS_NEWMASTER, node);
			return 0;
		}
		else {
			/* Back down */
			P_MEMB("Backing down from MASTER status\n");
			master_node = node;
			node_state = TRANSITION;

			/* If we were bringing a new node into the cluster then
			 * we will have to abandon that now and tell the new
			 * node to try again later */
			if (transitionreason == TRANS_NEWNODE && joining_node) {
				remove_joiner(1);
			}

			/* If the new master is not us OR the node we just got
			 * the STARTTRANS from then make sure it knows it has
			 * to be master */
			if (saddr->scl_nodeid != node->node_id) {
				send_nominate(startmsg, len, node->node_id);
				return 0;
			}

			/* Fall through into MEMBER code below if we are
			 * obeying the STARTTRANS we just received */
		}
	}

	/* Do non-MASTER STARTTRANS bits */
	if (node_state == MEMBER) {

		P_MEMB("Normal transition start\n");

		/* Save the master info */
		master_node = find_node_by_nodeid(saddr->scl_nodeid);
		node_state = TRANSITION;

		if (startmsg->reason == TRANS_NEWNODE) {
			add_node_from_starttrans(msg, buf, len);
		}

		send_startack(saddr, msg->msg_namelen);

		/* Establish timer in case the master dies */
		set_transition_timer(cman_config.transition_timeout);

		return 0;
	}

	/* We are in transition but this may be a restart */
	if (node_state == TRANSITION) {
		struct cluster_node *oldjoin = joining_node;

		master_node = find_node_by_nodeid(saddr->scl_nodeid);

		/* Is it a new joining node ? This happens if a master is
		 * usurped */
		if (startmsg->reason == TRANS_NEWNODE) {

			add_node_from_starttrans(msg, buf, len);
		}

		/* If this is a different node joining than the one we
		 * were previously joining (probably cos the master is
		 * a nominated one) then mark our "old" joiner as DEAD.
		 * The original master will already have told the node
		 * to go back into JOINWAIT state */
		if (oldjoin && oldjoin != joining_node &&
		    oldjoin->state == NODESTATE_JOINING)
			oldjoin->state = NODESTATE_DEAD;

		send_startack(saddr, msg->msg_namelen);

		/* Is it a new master node? */
		if (startmsg->reason == TRANS_NEWMASTER ||
		    startmsg->reason == TRANS_DEADMASTER) {
			P_MEMB("starttrans %s, node=%d\n",
			       startmsg->reason ==
			       TRANS_NEWMASTER ? "NEWMASTER" : "DEADMASTER",
			       le32_to_cpu(startmsg->nodeid));

			/* Store new master */
			master_node = find_node_by_nodeid(saddr->scl_nodeid);
		}


		/* Restart the timer */
		set_transition_timer(cman_config.transition_timeout);
	}

	return 0;
}


/* Change a cluster parameter */
static int do_process_reconfig(struct msghdr *msg, char *buf, int len)
{
	struct cl_mem_reconfig_msg *confmsg;
	struct sockaddr_cl *saddr = msg->msg_name;
	struct cluster_node *node;
	unsigned int val;

	if (len < sizeof(struct cl_mem_reconfig_msg))
		return -1;

	confmsg = (struct cl_mem_reconfig_msg *)buf;
	val = le32_to_cpu(confmsg->value);

	switch (confmsg->param) {

	case RECONFIG_PARAM_EXPECTED_VOTES:
		/* Set any nodes with expected_votes higher than the new value
		 * down */
		if (val > 0) {
			struct cluster_node *node;

			pthread_mutex_lock(&cluster_members_lock);
			list_iterate_items(node, &cluster_members_list) {
				if (node->state == NODESTATE_MEMBER &&
				    node->expected_votes > val) {
					node->expected_votes = val;
				}
			}
			pthread_mutex_unlock(&cluster_members_lock);
			if (expected_votes > val)
				expected_votes = val;
		}
		recalculate_quorum(1);	/* Allow decrease */
		break;

	case RECONFIG_PARAM_NODE_VOTES:
		node = find_node_by_nodeid(saddr->scl_nodeid);
		node->votes = val;
		recalculate_quorum(1);	/* Allow decrease */
		break;

	case RECONFIG_PARAM_CONFIG_VERSION:
		config_version = val;
		break;

	default:
		log_msg(LOG_ERR, "got unknown parameter in reconfigure message. %d\n",
		       confmsg->param);
		break;
	}
	return 0;
}

/* Response from master node */
static int do_process_joinack(struct msghdr *msg, char *buf, int len)
{
	struct cl_mem_joinack_msg *ackmsg =
		(struct cl_mem_joinack_msg *)buf;

	join_time = gettime();
	if (ackmsg->acktype == JOINACK_TYPE_OK) {
		node_state = JOINACK;
	}

	if (ackmsg->acktype == JOINACK_TYPE_NAK) {
		log_msg(LOG_INFO, "Cluster membership rejected\n");
		P_MEMB("Got JOINACK NACK\n");
		node_state = REJECTED;
	}

	if (ackmsg->acktype == JOINACK_TYPE_WAIT) {
		P_MEMB("Got JOINACK WAIT\n");
		node_state = JOINWAIT;
		joinwait_time = gettime();
	}

	return 0;
}

/* Check a JOINREQ message for validity,
   return -1 if we can't let the node join our cluster */
static int validate_joinmsg(struct cl_mem_join_msg *joinmsg, int len)
{
	struct cluster_node *node;

        /* Check version number */
	if (le32_to_cpu(joinmsg->major_version) == CNXMAN_MAJOR_VERSION) {
		char *ptr = (char *) joinmsg;
		char *name;

		ptr += sizeof (*joinmsg);
		name = ptr + le16_to_cpu(joinmsg->num_addr) * address_length;

		/* Sanity-check the num_addrs field otherwise we could oops */
		if (le16_to_cpu(joinmsg->num_addr) * address_length > len) {
			log_msg(LOG_ERR, "num_addr in JOIN-REQ message is rubbish: %d\n",
			       le16_to_cpu(joinmsg->num_addr));
			return -1;
		}

		/* Check the cluster name matches */
		if (strcmp(cluster_name, joinmsg->clustername)) {
			log_msg(LOG_ERR, "attempt to join with cluster name '%s' refused\n",
			       joinmsg->clustername);
			return -1;
		}

		/* Check we are not exceeding the maximum number of nodes */
		if (cluster_members >= cman_config.max_nodes) {
			log_msg(LOG_ERR, "Join request from %s rejected, exceeds maximum number of nodes\n",
			       name);
			return -1;
		}

		/* Check that we don't exceed the two_node limit, if applicable */
		if (two_node && cluster_members == 2) {
			log_msg(LOG_ERR, "Join request from %s "
				"rejected, exceeds two node limit\n", name);
			return -1;
		}

		if (le32_to_cpu(joinmsg->config_version) != config_version) {
			log_msg(LOG_ERR, "Join request from %s "
				"rejected, config version local %u remote %u\n",
				name, config_version,
				le32_to_cpu(joinmsg->config_version));
			return -1;
		}

		/* Validate requested static node ID */
		if (joinmsg->nodeid &&
		    (node = find_node_by_nodeid(le32_to_cpu(joinmsg->nodeid))) &&
		    (node->state != NODESTATE_DEAD ||
		     (strcmp(node->name, name)))) {
			log_msg(LOG_ERR, "Join request from %s "
			       "rejected, node ID %d already in use by %s\n",
			       name, node->node_id, node->name);
			return -1;
		}
		if (joinmsg->nodeid &&
		    (node = find_node_by_name(name)) &&
		    (node->state != NODESTATE_DEAD ||
		     node->node_id != le32_to_cpu(joinmsg->nodeid))) {
			log_msg(LOG_ERR, "Join request from %s "
			       "rejected, wanted node %d but previously had %d\n",
			       name, le32_to_cpu(joinmsg->nodeid), node->node_id);
			return -1;
		}

                /* If these don't match then I don't know how the message
		   arrived! However, I can't take the chance */
		if (le32_to_cpu(joinmsg->addr_len) != address_length) {
			log_msg(LOG_ERR, "Join request from %s "
			       "rejected, address length local: %u remote %u\n",
			       name, address_length,
			       le32_to_cpu(joinmsg->addr_len));
			return -1;
		}
	}
	else {
		/* Version number mismatch, don't use any part of the message
		 * other than the version numbers as things may have moved */
		log_msg(LOG_ERR, CMAN_NAME
		       "Got join message from node running incompatible software. (us: %d.%d.%d, them: %d.%d.%d)\n",
		       CNXMAN_MAJOR_VERSION, CNXMAN_MINOR_VERSION,
		       CNXMAN_PATCH_VERSION,
		       le32_to_cpu(joinmsg->major_version),
		       le32_to_cpu(joinmsg->minor_version),
		       le32_to_cpu(joinmsg->patch_version));
		return -1;
	}
	return 0;
}


/* Request to join the cluster. This makes us the master for this state
 * transition */
static int do_process_joinreq(struct msghdr *msg, char *buf, int len)
{
	static unsigned long last_joinreq = 0;
	static char last_name[MAX_CLUSTER_MEMBER_NAME_LEN];
	struct cl_mem_join_msg *joinmsg = (struct cl_mem_join_msg *)buf;
	struct cluster_node *node;
	char *ptr = (char *) joinmsg;
	char *name;
	int i;
	struct sockaddr_cl *addr = msg->msg_name;

	ptr += sizeof (*joinmsg);
	name = ptr + le16_to_cpu(joinmsg->num_addr) * address_length;

	/* If we are in a state transition then tell the new node to wait a bit
	 * longer */
	if (node_state != MEMBER) {
		if (node_state == MASTER || node_state == TRANSITION) {
			send_joinack((struct sockaddr_cl *)msg->msg_name, msg->msg_namelen,
				      JOINACK_TYPE_WAIT);
		}
		return 0;
	}

	/* Reject application if message is invalid for any reason */
	if (validate_joinmsg(joinmsg, len)) {
		send_joinack((struct sockaddr_cl *)msg->msg_name, msg->msg_namelen,
			     JOINACK_TYPE_NAK);
		return 0;
	}

	/* Do we already know about this node? */
	if (check_duplicate_node(name, msg, len) < 0) {
		send_joinack((struct sockaddr_cl *)msg->msg_name, msg->msg_namelen,
			     JOINACK_TYPE_NAK);
		return 0;
	}

	/* Duplicate checking: Because joining messages do not have
	 * sequence numbers we may get as many JOINREQ messages as we
	 * have interfaces. This bit of code here just checks for
	 * JOINREQ messages that come in from the same node in a small
	 * period of time and removes the duplicates */
	if (time_before(gettime(), last_joinreq + 10 )
	    && strcmp(name, last_name) == 0) {
		return 0;
	}

        /* OK, you can be in my gang */
	last_joinreq = gettime();
	strcpy(last_name, name);

	node = add_new_node(name, joinmsg->votes,
			    le32_to_cpu(joinmsg->expected_votes),
			    le32_to_cpu(joinmsg->nodeid),
			    NODESTATE_JOINING);

	/* A genuinely new node, assign it a genuinely new ID */
	if (node->node_id == 0) {
		set_nodeid(node, get_highest_nodeid()+1);
		highest_nodeid = node->node_id;
	}
	P_MEMB("New node %s has id %d\n", node->name, node->node_id);

	/* Add the node's addresses */
	if (list_empty(&node->addr_list)) {
		for (i = 0; i < le16_to_cpu(joinmsg->num_addr);
		     i++) {
			add_node_address(node,  (unsigned char *)ptr, address_length);
			ptr += address_length;
		}
	}
	send_joinack((struct sockaddr_cl *)msg->msg_name, msg->msg_namelen,
		     JOINACK_TYPE_OK);
	joining_node = node;
	joining_temp_nodeid = addr->scl_nodeid;

	/* Start the state transition */
	start_transition(TRANS_NEWNODE, node);

	return 0;
}

/* A simple function to invent a small number based
   on the node name */
static int node_hash(void)
{
	int i;
	int value = 0;

	for (i=0; i<strlen(nodename); i++) {
		value += nodename[i];
	}
	return (value & 0xF) + 1;
}


/* Return the low 32 bits of our IP address */
static uint32_t low32_of_ip()
{
	struct cluster_node_addr *addr;
	uint32_t lowip;

	addr = list_item(us->addr_list.n, struct cluster_node_addr);
	memcpy(&lowip, addr->addr+address_length-sizeof(uint32_t), sizeof(uint32_t));
	if (!lowip)
		memcpy(&lowip, addr->addr - sizeof(uint32_t)*2, sizeof(uint32_t));

	return lowip;
}

/* A new node has stated its intent to form a new cluster. we may have
 * something to say about that... */
static int do_process_newcluster(struct msghdr *msg, char *buf, int len)
{
	/* If we are also in STARTING state then back down for a random period
	 * of time */
	if (node_state == STARTING) {
		P_MEMB("got NEWCLUSTER, backing down for %d seconds\n", node_hash());
		start_time = gettime() + node_hash() ;
	}

	if (node_state == NEWCLUSTER) {
		uint32_t otherip;

		memcpy(&otherip, buf+1, sizeof(otherip));
		otherip = le32_to_cpu(otherip);
		P_MEMB("got NEWCLUSTER, remote ip = %x, us = %x\n", otherip, low32_of_ip());
		if (otherip < low32_of_ip())
			node_state = STARTING;
	}

	if (node_state == MEMBER)
		send_hello();

	return 0;
}

/* Called for each node by the node-message unpacker. Returns -1 if there is a
 * mismatch and the caller will stop processing */
static int check_node(struct cluster_node *newnode, char *addrs,
		      unsigned short num_addr)
{
	struct cluster_node *node = find_node_by_name(newnode->name);

	P_MEMB("check_node: %s", newnode->name);

	if (!node) {
		C_MEMB("  - not found\n");
		return -1;
	}

	/* Don't fail things if we have a node flagged as JOINING
	   but the master thinks is DEAD */
	if (node->votes != newnode->votes ||
	    node->node_id != newnode->node_id ||
	    (node->state != NODESTATE_JOINING &&
	     node->state != newnode->state)) {
		C_MEMB(" - wrong info: votes=%d(exp: %d) id=%d(exp: %d) state = %d(exp: %d)\n",
		       node->votes, newnode->votes, node->node_id,
		       newnode->node_id, node->state, newnode->state);
		return -1;
	}
	C_MEMB(" - OK\n");
	return 0;
}

/* Called for each new node found in a JOINCONF message. Create a new node
 * entry */
static int add_node(struct cluster_node *node, char *addrs,
		    unsigned short num_addr)
{
	P_MEMB("add_node: %s, v:%d, e:%d, i:%d\n", node->name, node->votes,
	       node->expected_votes, node->node_id);

	if (!find_node_by_name(node->name)) {
		struct cluster_node *newnode;
		int i;

		if ((newnode =
		     add_new_node(node->name, node->votes, node->expected_votes,
				  node->node_id, node->state)) == NULL) {
			P_MEMB("Error adding node\n");
			return -1;
		}
		if (list_empty(&newnode->addr_list)) {
			for (i = 0; i < num_addr; i++) {
				add_node_address(newnode,
						 (unsigned char *)addrs + i * address_length,
						 address_length);
			}
		}
		return 0;
	}
	else {
		P_MEMB("Already got node with name %s\n", node->name);
		return -1;
	}
}

/* Call a specified routine for each node unpacked from the message. Return
 * either the number of nodes found or -1 for an error */
static int unpack_nodes(char *buf, int len,
			int (*routine) (struct cluster_node *, char *,
					unsigned short))
{
	int ptr = 0;
	int num_nodes = 0;
	char nodename[MAX_CLUSTER_MEMBER_NAME_LEN];
	struct cluster_node node;

	node.name = nodename;

	while (ptr < len) {
		int namelen = buf[ptr++];
		unsigned int evotes;
		unsigned int node_id;
		unsigned short num_addr;
		char *addrs;

		memcpy(nodename, &buf[ptr], namelen);
		nodename[namelen] = '\0';
		ptr += namelen;

		node.state = buf[ptr++];

		memcpy(&num_addr, &buf[ptr], sizeof (short));
		num_addr = le16_to_cpu(num_addr);
		ptr += sizeof (short);

		/* Just make a note of the addrs "array" */
		addrs = &buf[ptr];
		ptr += num_addr * address_length;

		node.votes = buf[ptr++];

		memcpy(&evotes, &buf[ptr], sizeof (int));
		node.expected_votes = le32_to_cpu(evotes);
		ptr += sizeof (int);

		memcpy(&node_id, &buf[ptr], sizeof (int));
		node.node_id = le32_to_cpu(node_id);
		ptr += sizeof (int);

		/* Call the callback routine */
		if (routine(&node, addrs, num_addr) < 0)
			return -1;

		/* Return the number of MEMBER nodes */
		if (node.state == NODESTATE_MEMBER)
			num_nodes++;
	}
	return num_nodes;
}

/* Got join confirmation from a master node. This message contains a list of
 * cluster nodes which we unpack and build into our cluster nodes list. When we
 * have the last message we can go into TRANSITION state */
static int do_process_joinconf(struct msghdr *msg, char *buf, int len)
{
	if (unpack_nodes(buf + 2, len - 2, add_node) < 0) {
		log_msg(LOG_ERR, "Error procssing joinconf message - giving up on cluster join\n");
		us->leave_reason = CLUSTER_LEAVEFLAG_PANIC;
		node_state = LEFT_CLUSTER;
		return -1;
	}

	/* Last message in the list? */
	if (buf[1] & 2) {
		char ackmsg;
		struct sockaddr_cl *addr = msg->msg_name;

		us->state = NODESTATE_MEMBER;
		node_state = TRANSITION;
		we_are_a_cluster_member = TRUE;

		ackmsg = CLUSTER_MEM_CONFACK;

		cman_send_data(cman_handle, &ackmsg, 1, MSG_NOACK, 0, addr->scl_nodeid);

		pthread_create(&hello_pthread, NULL, hello_thread_fn, NULL);
	}
	return 0;
}

/* Got the master's view of the cluster - compare it with ours and tell it the
 * result */
static int do_process_masterview(struct msghdr *msg, char *buf, int len)
{
	char reply[2] = { CLUSTER_MEM_VIEWACK, 0 };
	struct sockaddr_cl *saddr = msg->msg_name;
	static int num_nodes;

	/* Someone else's state transition */
	if (node_state != MEMBER &&
	    node_state != TRANSITION && node_state != MASTER)
		return 0;

	/* First message, zero the counter */
	if (buf[1] & 1)
		num_nodes = 0;

	num_nodes += unpack_nodes(buf + 2, len - 2, check_node);

	/* Last message, check the count and reply */
	if (buf[1] & 2) {
		if (num_nodes == cluster_members) {
			/* Send ACK */
			reply[1] = 1;
		}
		else {
			P_MEMB("Got %d nodes in MASTERVIEW message, we think there s/b %d\n",
			     num_nodes, cluster_members);
			/* Send NAK */
			reply[1] = 0;
		}
		cman_send_data(cman_handle, reply, 2, 0, 0, saddr->scl_nodeid);
	}
	return 0;
}

static int do_process_leave(struct msghdr *msg, char *buf, int len)
{
	struct cluster_node *node;
	struct sockaddr_cl *saddr = msg->msg_name;
	unsigned char *leavemsg = (unsigned char *)buf;

	if ((node = find_node_by_nodeid(saddr->scl_nodeid))) {
		unsigned char reason = leavemsg[1];

		node->leave_reason = reason;
		leavereason = (reason == CLUSTER_LEAVEFLAG_REMOVED ? 1 : 0);

		a_node_just_died(node, 0);
	}
	return 0;
}

static int do_process_hello(struct msghdr *msg, char *buf, int len)
{
	struct cluster_node *node;
	struct cl_mem_hello_msg *hellomsg =
		(struct cl_mem_hello_msg *)buf;
	struct sockaddr_cl *saddr = msg->msg_name;

	/* We are starting up. Send a join message to the node whose HELLO we
	 * just received */
	if (node_state == STARTING || node_state == JOINWAIT ||
	    node_state == JOINING  || node_state == NEWCLUSTER) {
		struct sockaddr_cl *addr = msg->msg_name;

		log_msg(LOG_INFO, "sending membership request\n");

		send_joinreq(addr, msg->msg_namelen);
		join_time = gettime();
		node_state = JOINING;
		return 0;
	}

	/* Only process HELLOs if we are not in transition */
	if (node_state == MEMBER) {

		node = find_node_by_nodeid(saddr->scl_nodeid);
		if (node && node->state != NODESTATE_DEAD) {

			/* Check the cluster generation in the HELLO message.
			 * NOTE: this may be different if the message crossed
			 * on the wire with an END-TRANS so we allow a period
			 * of grace in which this is allowable */
			if (cluster_generation !=
			    le32_to_cpu(hellomsg->generation)
			    && node_state == MEMBER
			    && time_after(gettime(),
					  cman_config.hello_timer  +
					  transition_end_time)) {

				log_msg(LOG_INFO, CMAN_NAME
				       "bad generation number %d in HELLO message from %d, expected %d\n",
				       le32_to_cpu(hellomsg->generation),
				       saddr->scl_nodeid,
				       cluster_generation);

				start_transition(TRANS_CHECK, node);
				return 0;
			}

			if (cluster_members != le16_to_cpu(hellomsg->members)
			    && node_state == MEMBER) {
				log_msg(LOG_INFO, CMAN_NAME
				       "nmembers in HELLO message from %d does not match our view (got %d, exp %d)\n",
				       saddr->scl_nodeid,
				       le16_to_cpu(hellomsg->members),
				       cluster_members);
				start_transition(TRANS_CHECK, node);
				return 0;
			}
			/* The message is OK - save the time */
			node->last_hello = gettime();
		}
		else {
			/* This node is a danger to our valid cluster */
			if (cluster_is_quorate) {
				send_kill(saddr->scl_nodeid, 0);
			}
		}
	}

	return 0;

}

static int do_process_kill(struct msghdr *msg, char *buf, int len)
{
	struct sockaddr_cl *saddr = msg->msg_name;
	struct cluster_node *node;

	node = find_node_by_nodeid(saddr->scl_nodeid);
	if (node && node->state == NODESTATE_MEMBER) {

		log_msg(LOG_INFO, "Being told to leave the cluster by node %d\n",
		       saddr->scl_nodeid);

		node_state = LEFT_CLUSTER;
		quit_threads = 1;
		exit(2);
	}
	else {
		P_MEMB("Asked to leave the cluster by a non-member. What a nerve!\n");
	}
	return 0;
}

/* Some cluster membership utility functions */
struct cluster_node *find_node_by_name(char *name)
{
	struct list *nodelist;
	struct cluster_node *node;

	pthread_mutex_lock(&cluster_members_lock);
	list_iterate(nodelist, &cluster_members_list) {
		node = list_item(nodelist, struct cluster_node);

		if (strcmp(node->name, name) == 0) {
			pthread_mutex_unlock(&cluster_members_lock);
			return node;
		}
	}
	pthread_mutex_unlock(&cluster_members_lock);
	return NULL;
}

/* Try to avoid using this as it's slow and holds the members lock */
struct cluster_node *find_node_by_addr(char *addr, int addr_len)
{
	struct list *nodelist;
	struct list *addrlist;
	struct cluster_node *node;
	struct cluster_node_addr *nodeaddr;

	pthread_mutex_lock(&cluster_members_lock);

	list_iterate(nodelist, &cluster_members_list) {
		node = list_item(nodelist, struct cluster_node);

		list_iterate(addrlist, &node->addr_list) {
			nodeaddr =
				list_item(addrlist, struct cluster_node_addr);

			if (memcmp(nodeaddr->addr+2, addr+2, address_length-2) == 0) {
				pthread_mutex_unlock(&cluster_members_lock);
				return node;
			}
		}
	}

	pthread_mutex_unlock(&cluster_members_lock);
	return NULL;
}

/* This is the quick way to find a node */
struct cluster_node *find_node_by_nodeid(unsigned int id)
{
	struct cluster_node *node;

	if (id > sizeof_members_array)
		return NULL;

	pthread_mutex_lock(&members_by_nodeid_lock);
	node = members_by_nodeid[id];
	pthread_mutex_unlock(&members_by_nodeid_lock);
	return node;
}

/* Scan the nodes list for dead nodes */
static void check_for_dead_nodes()
{
	struct list *nodelist;
	struct cluster_node *node;

	P_MEMB("Checking for dead nodes\n");
	pthread_mutex_lock(&cluster_members_lock);
	list_iterate(nodelist, &cluster_members_list) {
		node = list_item(nodelist, struct cluster_node);

		if (node->state != NODESTATE_DEAD &&
		    time_after(gettime(),
			       node->last_hello +
			       cman_config.deadnode_timeout) && !node->us) {

			pthread_mutex_unlock(&cluster_members_lock);

			P_MEMB("dead node %s last hello was %ld, current time is %ld\n",
			       node->name, node->last_hello, gettime());

			node->leave_reason = CLUSTER_LEAVEFLAG_DEAD;
			leavereason = 0;

			/* This is unlikely to work but it's worth a try! */
			send_kill(node->node_id, 0);

			/* Start state transition */
			a_node_just_died(node, 0);
			return;
		}
	}
	pthread_mutex_unlock(&cluster_members_lock);

	/* Also check for a dead quorum device */
	if (quorum_device) {
		if (quorum_device->state == NODESTATE_MEMBER &&
		    time_after(gettime(),
			       quorum_device->last_hello +
			       cman_config.deadnode_timeout )) {
			quorum_device->state = NODESTATE_DEAD;
			log_msg(LOG_INFO, "Quorum device %s timed out\n",
			       quorum_device->name);
			recalculate_quorum(0);
		}
	}

	return;
}

/* add "us" as a node in the cluster */
static int add_us()
{
	struct cluster_node *newnode =
	    malloc(sizeof (struct cluster_node));

	if (!newnode) {
		/* Oh shit, we have to commit hara kiri here for the greater
		 * good of the cluster */
		send_leave(CLUSTER_LEAVEFLAG_PANIC);

		log_msg(LOG_INFO, "Cannot allocate memory for our node structure\n");
		exit(2);

		return -1;
	}

	memset(newnode, 0, sizeof (struct cluster_node));
	newnode->name = malloc(strlen(nodename) + 1);
	if (!newnode->name) {
		send_leave(CLUSTER_LEAVEFLAG_PANIC);

		log_msg(LOG_ERR, "Cannot allocate memory for node name\n");
		free(newnode);

		exit(2);

		return -1;
	}

	strcpy(newnode->name, nodename);
	newnode->last_hello = gettime();
	newnode->votes = votes;
	newnode->expected_votes = expected_votes;
	newnode->state = NODESTATE_JOINING;
	newnode->node_id = 0;	/* Will get filled in by ENDTRANS message */
	newnode->us = 1;
	newnode->leave_reason = 0;
	list_init(&newnode->addr_list);
	get_local_addresses(newnode);	/* Get from cnxman socket info */
	gettimeofday(&newnode->join_time, NULL);

	/* Add the new node to the list */
	pthread_mutex_lock(&cluster_members_lock);
	list_add(&cluster_members_list, &newnode->list);
	cluster_members++;
	pthread_mutex_unlock(&cluster_members_lock);
	us = newnode;

	return 0;
}

/* Return the highest known node_id */
unsigned int get_highest_nodeid()
{
	struct list *nodelist;
	struct cluster_node *node = NULL;
	unsigned int highest = 0;

	pthread_mutex_lock(&cluster_members_lock);
	list_iterate(nodelist, &cluster_members_list) {
		node = list_item(nodelist, struct cluster_node);

		if (node->node_id > highest)
			highest = node->node_id;
	}
	pthread_mutex_unlock(&cluster_members_lock);

	return highest;
}

/* Elect a new master if there is a clash. Returns 1 if we are the new master,
 * the master's struct will also be returned. This, rather primitively, uses
 * the lowest node ID */
static int elect_master(struct cluster_node **master_node, int disallow_node)
{
	int i;

	for (i = 1; i < sizeof_members_array; i++) {
		if (members_by_nodeid[i] &&
		    members_by_nodeid[i]->state == NODESTATE_MEMBER &&
		    i != disallow_node) {
			*master_node = members_by_nodeid[i];
			P_MEMB("Elected master is %s\n", (*master_node)->name);
			return (*master_node)->us;
		}
	}
	log_msg(LOG_CRIT, "Can't find a node to be transition master");
	exit(2);
}

/* Called by node_cleanup in cnxman when we have left the cluster */
void free_nodeid_array()
{
	free(members_by_nodeid);
	members_by_nodeid = NULL;
	sizeof_members_array = 0;
}

int allocate_nodeid_array()
{
	/* Allocate space for the nodeid lookup array */
	if (!members_by_nodeid) {
		pthread_mutex_init(&members_by_nodeid_lock, NULL);
		members_by_nodeid =
		    malloc(cman_config.max_nodes *
			    sizeof (struct cluster_member *));
	}

	if (!members_by_nodeid) {
		log_msg(LOG_ERR, "Unable to allocate members array for %d members\n",
		       cman_config.max_nodes);
		return ENOMEM;
	}
	memset(members_by_nodeid, 0,
	       cman_config.max_nodes * sizeof (struct cluster_member *));
	sizeof_members_array = cman_config.max_nodes;

	return 0;
}

/* Set the votes & expected_votes variables */
void set_votes(int v, int e)
{
	votes = v;
	expected_votes = e;
}

int get_quorum()
{
	return quorum;
}

/* Called by cnxman to see if activity should be blocked because we are in a
 * state transition */
int in_transition()
{
	return node_state == TRANSITION ||
	    node_state == TRANSITION_COMPLETE || node_state == MASTER;
}

char *leave_string(int reason)
{
	static char msg[32];
	switch (reason & 0xF)
	{
	case CLUSTER_LEAVEFLAG_DOWN:
		return "Shutdown";
	case CLUSTER_LEAVEFLAG_KILLED:
		return "Killed by another node";
	case CLUSTER_LEAVEFLAG_PANIC:
		return "Panic";
	case CLUSTER_LEAVEFLAG_REMOVED:
		return "Removed";
	case CLUSTER_LEAVEFLAG_REJECTED:
		return "Membership rejected";
	case CLUSTER_LEAVEFLAG_INCONSISTENT:
		return "Inconsistent cluster view";
	case CLUSTER_LEAVEFLAG_DEAD:
		return "Missed too many heartbeats";
	case CLUSTER_LEAVEFLAG_NORESPONSE:
		return "No response to messages";
	default:
		sprintf(msg, "Reason is %d\n", reason);
		return msg;
	}
}

#ifdef DEBUG
static char *msgname(int msg)
{
	switch (msg) {
	case CLUSTER_MEM_JOINCONF:
		return "JOINCONF";
	case CLUSTER_MEM_JOINREQ:
		return "JOINREQ";
	case CLUSTER_MEM_LEAVE:
		return "LEAVE";
	case CLUSTER_MEM_HELLO:
		return "HELLO";
	case CLUSTER_MEM_KILL:
		return "KILL";
	case CLUSTER_MEM_JOINACK:
		return "JOINACK";
	case CLUSTER_MEM_ENDTRANS:
		return "ENDTRANS";
	case CLUSTER_MEM_RECONFIG:
		return "RECONFIG";
	case CLUSTER_MEM_MASTERVIEW:
		return "MASTERVIEW";
	case CLUSTER_MEM_STARTTRANS:
		return "STARTTRANS";
	case CLUSTER_MEM_JOINREJ:
		return "JOINREJ";
	case CLUSTER_MEM_VIEWACK:
		return "VIEWACK";
	case CLUSTER_MEM_STARTACK:
		return "STARTACK";
	case CLUSTER_MEM_NEWCLUSTER:
		return "NEWCLUSTER";
	case CLUSTER_MEM_CONFACK:
		return "CONFACK";
	case CLUSTER_MEM_NOMINATE:
		return "NOMINATE";
	case CLUSTER_MEM_NODEDOWN:
		return "NODEDOWN";

	default:
		return "??UNKNOWN??";
	}
}

#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
