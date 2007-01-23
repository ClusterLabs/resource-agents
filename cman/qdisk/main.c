/**
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

  Author: Lon Hohberger <lhh at redhat.com>
 */
/**
  @file Main loop / functions for disk-based quorum daemon.
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <disk.h>
#include <platform.h>
#include <unistd.h>
#include <time.h>
#include <sys/reboot.h>
#include <sys/time.h>
#include <linux/reboot.h>
#include <sched.h>
#include <signal.h>
#include <ccs.h>
#include "score.h"
#include "clulog.h"
#if (!defined(LIBCMAN_VERSION) || \
     (defined(LIBCMAN_VERSION) && LIBCMAN_VERSION < 2))
#include <cluster/cnxman-socket.h>
#endif

int daemon_init(char *);
int check_process_running(char *, pid_t *);

/*
  TODO:
  1) Take into account timings to gracefully extend node timeouts during 
     node spikes (that's why they are there!).
  2) Poll ccsd for configuration changes.
  3) Logging.
 */

/* From bitmap.c */
int clear_bit(uint8_t *mask, uint32_t bitidx, uint32_t masklen);
int set_bit(uint8_t *mask, uint32_t bitidx, uint32_t masklen);
int is_bit_set(uint8_t *mask, uint32_t bitidx, uint32_t masklen);
inline int get_time(struct timeval *tv, int use_uptime);
inline void _diff_tv(struct timeval *dest, struct timeval *start,
		     struct timeval *end);

static int _running = 0;


static void
int_handler(int sig)
{
	_running = 0;
}


/**
  Simple thing to see if a node is running.
 */
inline int
state_run(disk_node_state_t state)
{
	return (state >= S_INIT ? state : 0);
}


/**
  Clear out / initialize node info block.
 */
void
node_info_init(node_info_t *ni, int max)
{
	int x;
	time_t t = time(NULL);

	memset(ni, 0, sizeof(*ni) * max);
	for (x = 0; x < max; x++) {
		ni[x].ni_status.ps_nodeid = (x + 1); /* node ids are 1-based */
		ni[x].ni_status.ps_timestamp = t;
		ni[x].ni_misses = 0;
		ni[x].ni_last_seen = t;
	}
}


/**
  Check to see if someone tried to evict us but we were out to lunch.
  Rare case; usually other nodes would put up the 'Undead' message and
  re-evict us.
 */
void
check_self(qd_ctx *ctx, status_block_t *sb)
{
	if (!sb->ps_updatenode ||
	    (sb->ps_updatenode == ctx->qc_my_id)) {
		return;
	}

	/* I did not update this??! */
	switch(sb->ps_state) {
	case S_EVICT:
		/* Someone told us to die. */
		reboot(RB_AUTOBOOT);
	default:
		clulog(LOG_EMERG, "Unhandled state: %d\n", sb->ps_state);
		raise(SIGSTOP);
	}
}


/**
  Read in the node blocks off of the quorum disk and see if anyone has
  or has not updated their timestamp recently.  See check_transitions as
  well.
 */
void
read_node_blocks(qd_ctx *ctx, node_info_t *ni, int max)
{
	int x;
	status_block_t *sb;

	for (x = 0; x < max; x++) {

		sb = &ni[x].ni_status;

		if (qdisk_read(ctx->qc_fd, qdisk_nodeid_offset(x+1),
			       sb, sizeof(*sb)) < 0) {
			clulog(LOG_WARNING,"Error reading node ID block %d\n",
			       x+1);
			continue;
		}
		swab_status_block_t(sb);

		if (sb->ps_nodeid == ctx->qc_my_id) {
			check_self(ctx, sb);
			continue;
		} 
		/* message. */
		ni[x].ni_msg.m_arg = sb->ps_arg;
		ni[x].ni_msg.m_msg = sb->ps_msg;
		ni[x].ni_msg.m_seq = sb->ps_seq;

		if (!state_run(sb->ps_state))
			continue;

		/* Unchanged timestamp: miss */
		if (sb->ps_timestamp == ni[x].ni_last_seen) {
			/* XXX check for average + allow grace */
			ni[x].ni_misses++;
			if (ni[x].ni_misses > 1) {
				clulog(LOG_DEBUG,
					"Node %d missed an update (%d/%d)\n",
					x+1, ni[x].ni_misses, ctx->qc_tko);
			}
			continue;
		}

		/* Got through?  The node is good. */
		ni[x].ni_misses = 0;
		ni[x].ni_seen++;
		ni[x].ni_last_seen = sb->ps_timestamp;
	}
}


/**
  Check for node transitions.
 */
void
check_transitions(qd_ctx *ctx, node_info_t *ni, int max, memb_mask_t mask)
{
	int x;

	if (mask)
		memset(mask, 0, sizeof(memb_mask_t));

	for (x = 0; x < max; x++) {

		/*
		   Case 1: check to see if the node is still up
		   according to our internal state, but has been
		   evicted by the master or cleanly shut down
		   (or restarted).

		   Transition from Evicted/Shutdown -> Offline
		 */
		if ((ni[x].ni_state >= S_EVICT &&
		     ni[x].ni_status.ps_state <= S_EVICT) ||
		     (ni[x].ni_incarnation &&
		      (ni[x].ni_incarnation !=
		       ni[x].ni_status.ps_incarnation))) {

			if (ni[x].ni_status.ps_state == S_EVICT) {
				clulog(LOG_NOTICE, "Node %d evicted\n",
				       ni[x].ni_status.ps_nodeid);
			} else {
				/* State == S_NONE or incarnation change */
				clulog(LOG_INFO, "Node %d shutdown\n",
				       ni[x].ni_status.ps_nodeid);
				ni[x].ni_evil_incarnation = 0;
			}

			ni[x].ni_incarnation = 0;
			ni[x].ni_seen = 0;
			ni[x].ni_misses = 0;
			ni[x].ni_state = S_NONE;

			/* Clear our master mask for the node after eviction
			 * or shutdown */
			if (mask)
				clear_bit(mask, (ni[x].ni_status.ps_nodeid-1),
					  sizeof(memb_mask_t));
			continue;
		}

		/*
		   Case 2: Check for a heartbeat timeout.  Write an eviction
		   notice if we're the master.  If this is our first notice
		   of the heartbeat timeout, update our internal state
		   accordingly.  When the master evicts this node, we will
		   hit case 1 above.

		   Transition from Online -> Evicted
		 */
		if (ni[x].ni_misses > ctx->qc_tko &&
		     state_run(ni[x].ni_status.ps_state)) {

			/*
			   Write eviction notice if we're the master.
			 */
			if (ctx->qc_status == S_MASTER) {
				clulog(LOG_NOTICE,
				       "Writing eviction notice for node %d\n",
				       ni[x].ni_status.ps_nodeid);
				qd_write_status(ctx, ni[x].ni_status.ps_nodeid,
						S_EVICT, NULL, NULL, NULL);
				if (ctx->qc_flags & RF_ALLOW_KILL) {
					clulog(LOG_DEBUG, "Telling CMAN to "
						"kill the node\n");
					cman_kill_node(ctx->qc_ch,
						ni[x].ni_status.ps_nodeid);
				}
			}

			/*
			   Mark our internal views as dead if nodes miss too
			   many heartbeats...  This will cause a master
			   transition if no live master exists.
			 */
			if (ni[x].ni_status.ps_state >= S_RUN &&
			    ni[x].ni_seen) {
				clulog(LOG_DEBUG, "Node %d DOWN\n",
				       ni[x].ni_status.ps_nodeid);
				ni[x].ni_seen = 0;	
			}

			ni[x].ni_state = S_EVICT;
			ni[x].ni_status.ps_state = S_EVICT;
			ni[x].ni_evil_incarnation = 
				ni[x].ni_status.ps_incarnation;
			
			/* Clear our master mask for the node after eviction */
			if (mask)
				clear_bit(mask, (ni[x].ni_status.ps_nodeid-1),
					  sizeof(memb_mask_t));
			continue;
		}

		/*
		   Case 3:  Check for node who is supposed to be dead, but
		   has started writing to the disk again with the same
		   incarnation.  

		   Transition from Offline -> Undead (BAD!!!)
		 */
		if (ni[x].ni_evil_incarnation &&
                    (ni[x].ni_evil_incarnation == 
		     ni[x].ni_status.ps_incarnation)) {
			clulog(LOG_CRIT, "Node %d is undead.\n",
			       ni[x].ni_status.ps_nodeid);

			clulog(LOG_ALERT,
			       "Writing eviction notice for node %d\n",
			       ni[x].ni_status.ps_nodeid);
			qd_write_status(ctx, ni[x].ni_status.ps_nodeid,
					S_EVICT, NULL, NULL, NULL);
			ni[x].ni_status.ps_state = S_EVICT;

			/* XXX Need to fence it again */
			if (ctx->qc_flags & RF_ALLOW_KILL) {
				clulog(LOG_DEBUG, "Telling CMAN to "
					"kill the node\n");
				cman_kill_node(ctx->qc_ch,
					ni[x].ni_status.ps_nodeid);
			}
			continue;
		}


		/*
		   Case 4:  Check for a node who has met our minimum # of
		   'seen' requests.

		   Transition from Offline -> Online
		 */
		if (ni[x].ni_seen > (ctx->qc_tko / 2) &&
		    !state_run(ni[x].ni_state)) {
			/*
			   Node-join - everyone just kind of "agrees"
			   there's no consensus to just have a node join
			   right now.
			 */
			ni[x].ni_state = S_RUN;
			clulog(LOG_DEBUG, "Node %d is UP\n",
			       ni[x].ni_status.ps_nodeid);
			ni[x].ni_incarnation =
			    ni[x].ni_status.ps_incarnation;
			if (mask)
				set_bit(mask, (ni[x].ni_status.ps_nodeid-1),
					sizeof(memb_mask_t));

			continue;
		}

		/*
		   Case 5: Check for a node becoming master.  Not really a
		   transition.
		 */
		if (ni[x].ni_state == S_RUN &&
		    ni[x].ni_status.ps_state == S_MASTER) {
			clulog(LOG_INFO, "Node %d is the master\n",
			       ni[x].ni_status.ps_nodeid);
			ni[x].ni_state = S_MASTER;
			if (mask)
				set_bit(mask, (ni[x].ni_status.ps_nodeid-1),
					sizeof(memb_mask_t));
			continue;
		}

		/*
		   All other cases: Believe the node's reported state ;)
		 */
		if (state_run(ni[x].ni_state)) {
			ni[x].ni_state = ni[x].ni_status.ps_state;
			if (mask)
				set_bit(mask, (ni[x].ni_status.ps_nodeid-1),
					sizeof(memb_mask_t));
		}
	}
}


/**
  Checks for presence of an online master.  If there is no
  Returns
 */
int
master_exists(qd_ctx *ctx, node_info_t *ni, int max, int *low_id)
{
	int x;
	int masters = 0;
	int ret = 0;

	*low_id = ctx->qc_my_id;

	for (x = 0; x < max; x++) {

		/* See if this one's a master */
		if (ni[x].ni_state >= S_RUN &&
		    ni[x].ni_status.ps_state == S_MASTER) {
			if (!ret)
				ret = ni[x].ni_status.ps_nodeid;
			++masters;
		}

		/* See if it's us... */
		if (ni[x].ni_status.ps_nodeid == ctx->qc_my_id &&
		    ni[x].ni_status.ps_state == S_MASTER) {
			if (!ret)
				ret = ctx->qc_my_id;
			++masters;
			continue;
		}

		/* Look for dead master */
		if (ni[x].ni_state < S_RUN &&
		    ni[x].ni_status.ps_state == S_MASTER) {
			clulog(LOG_DEBUG,
			       "Node %d is marked master, but is dead.\n",
			       ni[x].ni_status.ps_nodeid);
			continue;
		}

		if (ni[x].ni_state < S_RUN)
			continue;
		
		if (ni[x].ni_status.ps_nodeid < *low_id)
			*low_id = ni[x].ni_status.ps_nodeid;
	}

	if (masters > 1) {
		clulog(LOG_CRIT,
		       "Critical Error: More than one master found!\n");
		/* XXX Handle this how? */
	}
	/*
 	else if (masters == 1) {
		printf("Node %d is the master\n", ret);
	} else {
		printf("No master found; node %d should be the master\n",
		       *low_id);
	}
	*/

	return ret;
}


/**
  initialize node information blocks and wait to see if there is already
  a cluster running using this QD.  Note that this will delay master
  election if multiple nodes start with a second or two of each other.
 */
int
quorum_init(qd_ctx *ctx, node_info_t *ni, int max, struct h_data *h, int maxh)
{
	int x = 0, score, maxscore;

	clulog(LOG_INFO, "Quorum Daemon Initializing\n");
	
	if (mlockall(MCL_CURRENT|MCL_FUTURE) != 0) {
		clulog(LOG_ERR, "Unable to mlockall()\n");
	}

	if (qdisk_validate(ctx->qc_device) < 0)
		return -1;

	ctx->qc_fd = qdisk_open(ctx->qc_device);
	if (ctx->qc_fd < 0) {
		clulog(LOG_CRIT, "Failed to open %s: %s\n", ctx->qc_device,
		       strerror(errno));
		return -1;
	}
	
	start_score_thread(ctx, h, maxh);

	node_info_init(ni, max);
	if (qd_write_status(ctx, ctx->qc_my_id,
			    S_INIT, NULL, NULL, NULL) != 0) {
		clulog(LOG_CRIT, "Could not initialize status block!\n");
		return -1;
	}

	while (++x <= ctx->qc_tko) {
		read_node_blocks(ctx, ni, max);
		check_transitions(ctx, ni, max, NULL);

		if (qd_write_status(ctx, ctx->qc_my_id,
				    S_INIT, NULL, NULL, NULL) != 0) {
			clulog(LOG_CRIT, "Initialization failed\n");
			return -1;
		}

		sleep(ctx->qc_interval);
	}

	get_my_score(&score,&maxscore);
	clulog(LOG_INFO, "Initial score %d/%d\n", score, maxscore);
	clulog(LOG_INFO, "Initialization complete\n");

	return 0;
}


/**
  Vote for a master if it puts a bid in.
 */
void
do_vote(qd_ctx *ctx, node_info_t *ni, int max, disk_msg_t *msg)
{
	int x;

	for (x = 0; x < max; x++) {
		if (ni[x].ni_state != S_RUN)
			continue;

		if (ni[x].ni_status.ps_msg == M_BID &&
		    ni[x].ni_status.ps_nodeid < ctx->qc_my_id) {

			/* Vote for lowest bidding ID that is lower
			   than us */
			msg->m_msg = M_ACK;
			msg->m_arg = ni[x].ni_status.ps_nodeid;
			msg->m_seq = ni[x].ni_status.ps_seq;

			return;
		}
	}
}


/*
  Check to match nodes in mask with nodes online according to CMAN.
  Only the master needs to do this.
 */
void
check_cman(qd_ctx *ctx, memb_mask_t mask, memb_mask_t master_mask)
{
	cman_node_t nodes[MAX_NODES_DISK];
	int retnodes, x;

	if (cman_get_nodes(ctx->qc_ch, MAX_NODES_DISK,
			   &retnodes, nodes) <0 )
		return;

	memset(master_mask, 0, sizeof(master_mask));
	for (x = 0; x < retnodes; x++) {
		if (is_bit_set(mask, nodes[x].cn_nodeid-1, sizeof(mask)) &&
		    nodes[x].cn_member) {
			set_bit(master_mask, nodes[x].cn_nodeid-1,
				sizeof(master_mask));
		} else {
			/* Not in CMAN output = not allowed */
			clear_bit(master_mask, (nodes[x].cn_nodeid-1),
				  sizeof(memb_mask_t));
		}
	}
}


/* 
   returns:
	3: all acks received - you are the master.
	2: nacked (not highest score?) might not happen
	1: other node with lower ID is bidding and we should rescind our
	   bid.
	0: still waiting; don't clear bid; just wait another round.
   Modifies:
	*msg - it will store the vote for the lowest bid if we should
	clear our bid.
 */ 
int
check_votes(qd_ctx *ctx, node_info_t *ni, int max, disk_msg_t *msg)
{
	int x, running = 0, acks = 0, nacks = 0, low_id = ctx->qc_my_id;

	for (x = 0; x < max; x++) {
		if (state_run(ni[x].ni_state))
			++running;
		else
			continue;

		if (ni[x].ni_status.ps_msg == M_ACK &&
		    ni[x].ni_status.ps_arg == ctx->qc_my_id) {
			++acks;
		}

		if (ni[x].ni_status.ps_msg == M_NACK &&
		    ni[x].ni_status.ps_arg == ctx->qc_my_id) {
			++nacks;
		}
		
		/* If there's someone with a lower ID who is also
		   bidding for master, change our message to vote
		   for the lowest bidding node ID */
		if (ni[x].ni_status.ps_msg == M_BID && 
		    ni[x].ni_status.ps_nodeid < low_id) {
			low_id = ni[x].ni_status.ps_nodeid;
			msg->m_msg = M_ACK;
			msg->m_arg = ni[x].ni_status.ps_nodeid;
			msg->m_seq = ni[x].ni_status.ps_seq;
		}
	}

	if (acks == running)
		return 3;
	if (nacks)
		return 2;
	if (low_id != ctx->qc_my_id)
		return 1;
	return 0;
}


char *
state_str(disk_node_state_t s)
{
	switch (s) {
	case S_NONE:
		return "None";
	case S_EVICT:
		return "Evicted";
	case S_INIT:
		return "Initializing";
	case S_RUN:
		return "Running";
	case S_MASTER:
		return "Master";
	default:
		return "ILLEGAL";
	}
}


void
update_local_status(qd_ctx *ctx, node_info_t *ni, int max, int score,
		    int score_req, int score_max)
{
	FILE *fp;
	int x, need_close = 0;

	if (!ctx->qc_status_file)
		return;

	if (strcmp(ctx->qc_status_file, "-") == 0) {
		fp = stdout;
	} else {
		fp = fopen(ctx->qc_status_file, "w+");
		if (fp == NULL)
			return;
		need_close = 1;
	}

	fprintf(fp, "Node ID: %d\n", ctx->qc_my_id);
	
	if (ctx->qc_master)
		fprintf(fp, "Master Node ID: %d\n", ctx->qc_master);
	else 
		fprintf(fp, "Master Node ID: (none)\n");
	
	fprintf(fp, "Score: %d/%d (Minimum required = %d)\n",
		score, score_max, score_req);
	fprintf(fp, "Current state: %s\n", state_str(ctx->qc_status));
	fprintf(fp, "Current disk state: %s\n",
		state_str(ctx->qc_disk_status));

	fprintf(fp, "Initializing Set: {");
	for (x=0; x<max; x++) {
		if (ni[x].ni_state == S_INIT)
			fprintf(fp," %d", ni[x].ni_status.ps_nodeid);
	}
	fprintf(fp, " }\n");
	
	fprintf(fp, "Visible Set: {");
	for (x=0; x<max; x++) {
		if (ni[x].ni_state >= S_RUN || ni[x].ni_status.ps_nodeid == 
		    ctx->qc_my_id)
			fprintf(fp," %d", ni[x].ni_status.ps_nodeid);
	}
	fprintf(fp, " }\n");
	
	if (!ctx->qc_master)
		goto out;

	fprintf(fp, "Quorate Set: {");
	for (x=0; x<max; x++) {
		if (is_bit_set(ni[ctx->qc_master-1].ni_status.ps_master_mask,
			       ni[x].ni_status.ps_nodeid-1,
			       sizeof(memb_mask_t))) {
			fprintf(fp," %d", ni[x].ni_status.ps_nodeid);
		}
	}

	fprintf(fp, " }\n");

out:
	fprintf(fp, "\n");
	if (need_close)
		fclose(fp);
}


/* Timeval functions from clumanager */
/**
 * Scale a (struct timeval).
 *
 * @param tv		The timeval to scale.
 * @param scale		Positive multiplier.
 * @return		tv
 */
struct timeval *
_scale_tv(struct timeval *tv, int scale)
{
	tv->tv_sec *= scale;
	tv->tv_usec *= scale;

	if (tv->tv_usec > 1000000) {
		tv->tv_sec += (tv->tv_usec / 1000000);
		tv->tv_usec = (tv->tv_usec % 1000000);
	}

	return tv;
}



#define _print_tv(val) \
	printf("%s: %d.%06d\n", #val, (int)((val)->tv_sec), \
			(int)((val)->tv_usec))


static inline int
_cmp_tv(struct timeval *left, struct timeval *right)
{
	if (left->tv_sec > right->tv_sec)
		return -1;

	if (left->tv_sec < right->tv_sec)
		return 1;

	if (left->tv_usec > right->tv_usec)
		return -1;
	
	if (left->tv_usec < right->tv_usec)
		return 1;

	return 0;
}


void
set_priority(int queue, int prio)
{
	struct sched_param s;
	int ret;
	char *func = "nice";
	
	if (queue == SCHED_OTHER) {
		s.sched_priority = 0;
		ret = sched_setscheduler(0, queue, &s);
		errno = 0;
		ret = nice(prio);
	} else {
		memset(&s,0,sizeof(s));
		s.sched_priority = prio;
		ret = sched_setscheduler(0, queue, &s);
		func = "sched_setscheduler";
	}
	
	if (ret < 0 && errno) {
		clulog(LOG_WARNING, "set_priority [%s] failed: %s\n", func,
		       strerror(errno));
	}
}


int
cman_alive(cman_handle_t ch)
{
	fd_set rfds;
	int fd = cman_get_fd(ch);
	struct timeval tv = {0, 0};
	
	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);
	if (select(fd + 1, &rfds, NULL, NULL, &tv) == 1) {
		if (cman_dispatch(ch, CMAN_DISPATCH_ALL) < 0) {
			if (errno == EAGAIN)
				return 0;
			return -1;
		}
	}
	return 0;
}


int
quorum_loop(qd_ctx *ctx, node_info_t *ni, int max)
{
	disk_msg_t msg = {0, 0, 0};
	int low_id, bid_pending = 0, score, score_max, score_req,
	    upgrade = 0;
	memb_mask_t mask, master_mask;
	struct timeval maxtime, oldtime, newtime, diff, sleeptime, interval;

	ctx->qc_status = S_NONE;
	
	maxtime.tv_usec = 0;
	maxtime.tv_sec = ctx->qc_interval * ctx->qc_tko;
	
	interval.tv_usec = 0;
	interval.tv_sec = ctx->qc_interval;
	
	get_my_score(&score, &score_max);
	if (score_max < ctx->qc_scoremin) {
		clulog(LOG_WARNING, "Minimum score (%d) is impossible to "
		       "achieve (heuristic total = %d)\n",
		       ctx->qc_scoremin, score_max);
	}
	
	_running = 1;
	while (_running) {
		/* XXX this was getuptime() in clumanager */
		get_time(&oldtime, (ctx->qc_flags&RF_UPTIME));
		
		/* Read everyone else's status */
		read_node_blocks(ctx, ni, max);

		/* Check for node transitions */
		check_transitions(ctx, ni, max, mask);

		/* Check heuristics and remove ourself if necessary */
		get_my_score(&score, &score_max);
		upgrade = 0;

		score_req = ctx->qc_scoremin;
		if (score_req <= 0)
			score_req = (score_max/2 + 1);

		if (score < score_req) {
			clear_bit(mask, (ctx->qc_my_id-1), sizeof(mask));
			if (ctx->qc_status > S_NONE) {
				clulog(LOG_NOTICE,
				       "Score insufficient for master "
				       "operation (%d/%d; required=%d); "
				       "downgrading\n",
				       score, score_max, score_req);
				ctx->qc_status = S_NONE;
				msg.m_msg = M_NONE;
				++msg.m_seq;
				bid_pending = 0;
				if (cman_alive(ctx->qc_ch) < 0) {
					clulog(LOG_ERR, "cman: %s\n",
					       strerror(errno));
				} else {
					cman_poll_quorum_device(ctx->qc_ch, 0);
				}
				if (ctx->qc_flags & RF_REBOOT)
					reboot(RB_AUTOBOOT);
			}
		}  else {
			set_bit(mask, (ctx->qc_my_id-1), sizeof(mask));
			if (ctx->qc_status == S_NONE) {
				clulog(LOG_NOTICE,
				       "Score sufficient for master "
				       "operation (%d/%d; required=%d); "
				       "upgrading\n",
				       score, score_max, score_req);
				ctx->qc_status = S_RUN;
				upgrade = (ctx->qc_tko / 3);
				if (upgrade == 0)
					upgrade = 1;
			}
		}

		/* Find master */
		ctx->qc_master = master_exists(ctx, ni, max, &low_id);

		/* Figure out what to do based on what we know */
		if (!ctx->qc_master &&
		    low_id == ctx->qc_my_id &&
		    ctx->qc_status == S_RUN &&
		    !bid_pending &&
		    !upgrade) {
			/*
			   If there's no master, and we are the lowest node
			   ID, make a bid to become master if we're not 
			   already bidding.  We can't do this if we've just
			   upgraded.
			 */

			clulog(LOG_DEBUG,"Making bid for master\n");
			msg.m_msg = M_BID;
			++msg.m_seq;
			bid_pending = 1;

		} else if (!ctx->qc_master && !bid_pending) {

			/* We're not the master, and we do not have a bid
			   pending.  Check for voting on other nodes. */
			do_vote(ctx, ni, max, &msg);
		} else if (!ctx->qc_master && bid_pending) {

			/* We're currently bidding for master.
			   See if anyone's voted, or if we should
			   rescind our bid */
			++bid_pending;

			/* Yes, those are all deliberate fallthroughs */
			switch (check_votes(ctx, ni, max, &msg)) {
			case 3:
				/* 
				 * Give ample time to become aware of other
				 * nodes
				 */
				if (bid_pending < (ctx->qc_tko / 3))
					break;
				
				clulog(LOG_INFO,
				       "Assuming master role\n");
				ctx->qc_status = S_MASTER;
			case 2:
				msg.m_msg = M_NONE;
			case 1:
				bid_pending = 0;
			default:
				break;
			}
		} else if (ctx->qc_status == S_MASTER &&
			   ctx->qc_master != ctx->qc_my_id) {
			
			/* We think we're master, but someone else claims
			   that they are master. */

			clulog(LOG_CRIT,
			       "A master exists, but it's not me?!\n");
			/* XXX Handle this how? Should not happen*/
			/* reboot(RB_AUTOBOOT); */

		} else if (ctx->qc_status == S_MASTER &&
			   ctx->qc_master == ctx->qc_my_id) {

			/* We are the master.  Poll the quorum device.
			   We can't be the master unless we score high
			   enough on our heuristics. */
			if (cman_alive(ctx->qc_ch) < 0) {
				clulog(LOG_ERR, "cman_dispatch: %s\n",
				       strerror(errno));
				clulog(LOG_ERR,
				       "Halting qdisk operations\n");
				return -1;
			}
			check_cman(ctx, mask, master_mask);
			cman_poll_quorum_device(ctx->qc_ch, 1);

		} else if (ctx->qc_status == S_RUN && ctx->qc_master &&
			   ctx->qc_master != ctx->qc_my_id) {

			/* We're not the master, but a master exists
			   Check to see if the master thinks we are 
			   online.  If we are, tell CMAN so. */
			if (is_bit_set(
			      ni[ctx->qc_master-1].ni_status.ps_master_mask,
				       ctx->qc_my_id-1,
				       sizeof(memb_mask_t))) {
				if (cman_alive(ctx->qc_ch) < 0) {
					clulog(LOG_ERR, "cman_dispatch: %s\n",
						strerror(errno));
					clulog(LOG_ERR,
						"Halting qdisk operations\n");
					return -1;
				}
				cman_poll_quorum_device(ctx->qc_ch, 1);
			}
		}
		
		/* Write out our status */
		if (qd_write_status(ctx, ctx->qc_my_id, ctx->qc_status,
				    &msg, mask, master_mask) != 0) {
			clulog(LOG_ERR, "Error writing to quorum disk\n");
		}

		/* write out our local status */
		update_local_status(ctx, ni, max, score, score_req, score_max);

		/* Cycle. We could time the loop and sleep
		   usleep(interval-looptime), but this is fine for now.*/
		get_time(&newtime, ctx->qc_flags&RF_UPTIME);
		_diff_tv(&diff, &oldtime, &newtime);
		
		/*
		 * Reboot if we didn't send a heartbeat in interval*TKO_COUNT
		 */
		if (_cmp_tv(&maxtime, &diff) == 1 &&
		    ctx->qc_flags & RF_PARANOID) {
			clulog(LOG_EMERG, "Failed to complete a cycle within "
			       "%d second%s (%d.%06d) - REBOOTING\n",
			       (int)maxtime.tv_sec,
			       maxtime.tv_sec==1?"":"s",
			       (int)diff.tv_sec,
			       (int)diff.tv_usec);
			if (!(ctx->qc_flags & RF_DEBUG)) 
				reboot(RB_AUTOBOOT);
		}

		/*
		 * If the amount we took to complete a loop is greater or less
		 * than our interval, we adjust by the difference each round.
		 *
		 * It's not really "realtime", but it helps!
		 */
		if (_cmp_tv(&diff, &interval) == 1) {
			_diff_tv(&sleeptime, &diff, &interval);
		} else {
			clulog(LOG_WARNING, "qdisk cycle took more "
			       "than %d second%s to complete (%d.%06d)\n",
			       ctx->qc_interval, ctx->qc_interval==1?"":"s",
			       (int)diff.tv_sec, (int)diff.tv_usec);
			memcpy(&sleeptime, &interval, sizeof(sleeptime));
		}
		
		/* Could hit a watchdog timer here if we wanted to */
		if (_running)
			select(0, NULL, NULL, NULL, &sleeptime);
	}

	return 0;
}


/**
  Tell the other nodes we're done (safely!).
 */
int
quorum_logout(qd_ctx *ctx)
{
	/* Write out our status */
	if (qd_write_status(ctx, ctx->qc_my_id, S_NONE,
			    NULL, NULL, NULL) != 0) {
		clulog(LOG_WARNING,
		       "Error writing to quorum disk during logout\n");
	}
	return 0;
}


/**
  Grab all our configuration data from CCSD
 */
int
get_config_data(char *cluster_name, qd_ctx *ctx, struct h_data *h, int maxh,
		int *cfh, int debug)
{
	int ccsfd = -1, loglevel = 4;
	char query[256];
	char *val;

	clulog(LOG_DEBUG, "Loading configuration information\n");

	ccsfd = ccs_force_connect(cluster_name, 1);
	if (ccsfd < 0) {
		clulog(LOG_CRIT, "Connection to CCSD failed; cannot start\n");
		return -1;
	}

	ctx->qc_interval = 1;
	ctx->qc_tko = 10;
	ctx->qc_scoremin = 0;
	ctx->qc_flags = RF_REBOOT | RF_ALLOW_KILL | RF_UPTIME;
			/* | RF_STOP_CMAN;*/
	ctx->qc_sched = SCHED_RR;
	ctx->qc_sched_prio = 1;

	/* Get log log_facility */
	snprintf(query, sizeof(query), "/cluster/quorumd/@log_facility");
	if (ccs_get(ccsfd, query, &val) == 0) {
		clu_set_facility(val);
		clulog(LOG_DEBUG, "Log facility: %s\n", val);
		free(val);
	}

	/* Get log level */
	snprintf(query, sizeof(query), "/cluster/quorumd/@log_level");
	if (ccs_get(ccsfd, query, &val) == 0) {
		loglevel = atoi(val);
		free(val);
		if (loglevel < 0)
			loglevel = 4;

		if (!debug)
			clu_set_loglevel(loglevel);
	}

	/* Get interval */
	snprintf(query, sizeof(query), "/cluster/quorumd/@interval");
	if (ccs_get(ccsfd, query, &val) == 0) {
		ctx->qc_interval = atoi(val);
		free(val);
		if (ctx->qc_interval < 1)
			ctx->qc_interval = 1;
	}
		
	/* Get tko */
	snprintf(query, sizeof(query), "/cluster/quorumd/@tko");
	if (ccs_get(ccsfd, query, &val) == 0) {
		ctx->qc_tko = atoi(val);
		free(val);
		if (ctx->qc_tko < 3)
			ctx->qc_tko = 3;
	}
		
	/* Get votes */
	snprintf(query, sizeof(query), "/cluster/quorumd/@votes");
	if (ccs_get(ccsfd, query, &val) == 0) {
		ctx->qc_votes = atoi(val);
		free(val);
		if (ctx->qc_votes < 0)
			ctx->qc_votes = 0;
	}

	/* Get device */
	snprintf(query, sizeof(query), "/cluster/quorumd/@device");
	if (ccs_get(ccsfd, query, &val) == 0) {
		ctx->qc_device = val;
	}

	/* Get label (overrides device) */
	snprintf(query, sizeof(query), "/cluster/quorumd/@label");
	if (ccs_get(ccsfd, query, &val) == 0) {
		ctx->qc_label = val;
	}

	/* Get status file */
	snprintf(query, sizeof(query), "/cluster/quorumd/@status_file");
	if (ccs_get(ccsfd, query, &val) == 0) {
		ctx->qc_status_file = val;
	}

	/* Get min score */
	snprintf(query, sizeof(query), "/cluster/quorumd/@min_score");
	if (ccs_get(ccsfd, query, &val) == 0) {
		ctx->qc_scoremin = atoi(val);
		free(val);
		if (ctx->qc_scoremin < 0)
			ctx->qc_scoremin = 0;
	}
	
	/* Get scheduling queue */
	snprintf(query, sizeof(query), "/cluster/quorumd/@scheduler");
	if (ccs_get(ccsfd, query, &val) == 0) {
		switch(val[0]) {
		case 'r':
		case 'R':
			ctx->qc_sched = SCHED_RR;
			break;
		case 'f':
		case 'F':
			ctx->qc_sched = SCHED_FIFO;
			break;
		case 'o':
		case 'O':
			ctx->qc_sched = SCHED_OTHER;
			break;
		default:
			clulog(LOG_WARNING, "Invalid scheduling queue '%s'\n",
			       val);
			break;
		}
		free(val);
	}
	
	/* Get priority */
	snprintf(query, sizeof(query), "/cluster/quorumd/@priority");
	if (ccs_get(ccsfd, query, &val) == 0) {
		ctx->qc_sched_prio = atoi(val);
		free(val);
	}	

	/* Get reboot flag for when we transition -> offline */
	/* default = on, so, 0 to turn off */
	snprintf(query, sizeof(query), "/cluster/quorumd/@reboot");
	if (ccs_get(ccsfd, query, &val) == 0) {
		if (!atoi(val))
			ctx->qc_flags &= ~RF_REBOOT;
		free(val);
	}
	
	/*
	 * Get flag to see if we're supposed to kill cman if qdisk is not 
	 * available.
	 */
	/* default = off, so, 1 to turn on */
	snprintf(query, sizeof(query), "/cluster/quorumd/@stop_cman");
	if (ccs_get(ccsfd, query, &val) == 0) {
		if (!atoi(val))
			ctx->qc_flags &= ~RF_STOP_CMAN;
		else
			ctx->qc_flags |= RF_STOP_CMAN;
		free(val);
	}
	
	
	/*
	 * Get flag to see if we're supposed to reboot if we can't complete
	 * a pass in failure time
	 */
	/* default = off, so, 1 to turn on */
	snprintf(query, sizeof(query), "/cluster/quorumd/@paranoid");
	if (ccs_get(ccsfd, query, &val) == 0) {
		if (!atoi(val))
			ctx->qc_flags &= ~RF_PARANOID;
		else
			ctx->qc_flags |= RF_PARANOID;
		free(val);
	}
	
	
	/*
	 * Get flag to see if we're supposed to reboot if we can't complete
	 * a pass in failure time
	 */
	/* default = off, so, 1 to turn on */
	snprintf(query, sizeof(query), "/cluster/quorumd/@allow_kill");
	if (ccs_get(ccsfd, query, &val) == 0) {
		if (!atoi(val))
			ctx->qc_flags &= ~RF_ALLOW_KILL;
		else
			ctx->qc_flags |= RF_ALLOW_KILL;
		free(val);
	}

	/*
	 * Get flag to see if we're supposed to use /proc/uptime instead of
	 * gettimeofday(2)
	 */
	/* default = off, so, 1 to turn on */
	snprintf(query, sizeof(query), "/cluster/quorumd/@use_uptime");
	if (ccs_get(ccsfd, query, &val) == 0) {
		if (!atoi(val))
			ctx->qc_flags &= ~RF_UPTIME;
		else
			ctx->qc_flags |= RF_UPTIME;
		free(val);
	}

	*cfh = configure_heuristics(ccsfd, h, maxh);

	clulog(LOG_DEBUG,
	       "Quorum Daemon: %d heuristics, %d interval, %d tko, %d votes\n",
	       *cfh, ctx->qc_interval, ctx->qc_tko, ctx->qc_votes);

	ccs_disconnect(ccsfd);

	return 0;
}


void
check_stop_cman(qd_ctx *ctx)
{
	if (!(ctx->qc_flags & RF_STOP_CMAN))
		return;
	
	clulog(LOG_WARNING, "Telling CMAN to leave the cluster; qdisk is not"
		" available\n");
#if (defined(LIBCMAN_VERSION) && LIBCMAN_VERSION >= 2)
	if (cman_shutdown(ctx->qc_ch, 0) < 0) {
#else
	int x = 0;
	if (ioctl(cman_get_fd(ctx->qc_ch), SIOCCLUSTER_LEAVE_CLUSTER, &x) < 0) {
#endif
		clulog(LOG_CRIT, "Could not leave the cluster - rebooting\n");
		sleep(5);
		if (ctx->qc_flags & RF_DEBUG)
			return;
		reboot(RB_AUTOBOOT);
	}
}


int
main(int argc, char **argv)
{
	cman_node_t me;
	int cfh, rv, forked = 0;
	qd_ctx ctx;
	cman_handle_t ch;
	node_info_t ni[MAX_NODES_DISK];
	struct h_data h[10];
	char debug = 0, foreground = 0;
	char device[128];
	pid_t pid;
	
	if (check_process_running(argv[0], &pid) && pid !=getpid()) {
		printf("QDisk services already running\n");
		return 0;
	}
	
	while ((rv = getopt(argc, argv, "fd")) != EOF) {
		switch (rv) {
		case 'd':
			debug = 1;
			break;
		case 'f':
			foreground = 1;
			clu_log_console(1);
		default:
			break;
		}
	}
	
#if (defined(LIBCMAN_VERSION) && LIBCMAN_VERSION >= 2)
	ch = cman_admin_init(NULL);
#else
	ch = cman_init(NULL);
#endif
	if (!ch) {
		if (!foreground && !forked) {
			if (daemon_init(argv[0]) < 0)
				return -1;
			else
				forked = 1;
		}
		
		clulog(LOG_INFO, "Waiting for CMAN to start\n");
		
		do {
			sleep(5);
#if (defined(LIBCMAN_VERSION) && LIBCMAN_VERSION >= 2)
			ch = cman_admin_init(NULL);
#else
			ch = cman_init(NULL);
#endif
		} while (!ch);
	}

	memset(&me, 0, sizeof(me));
	while (cman_get_node(ch, CMAN_NODEID_US, &me) < 0) {
		if (!foreground && !forked) {
			if (daemon_init(argv[0]) < 0)
				return -1;
			else
				forked = 1;
		}
		sleep(5);
	}

	qd_init(&ctx, ch, me.cn_nodeid);

	signal(SIGINT, int_handler);
	signal(SIGTERM, int_handler);

        if (debug) {
                clu_set_loglevel(LOG_DEBUG);
                ctx.qc_flags |= RF_DEBUG;
        }
		
	if (get_config_data(NULL, &ctx, h, 10, &cfh, debug) < 0) {
		clulog_and_print(LOG_CRIT, "Configuration failed\n");
		check_stop_cman(&ctx);
		return -1;
	}
	
	if (ctx.qc_label) {
		if (find_partitions("/proc/partitions",
				    ctx.qc_label, device,
				    sizeof(device), 0) != 0) {
			clulog_and_print(LOG_CRIT, "Unable to match label"
					 " '%s' to any device\n",
					 ctx.qc_label);
			check_stop_cman(&ctx);
			return -1;
		}

		if (ctx.qc_device)
			free(ctx.qc_device);

		ctx.qc_device = strdup(device);

		clulog(LOG_INFO, "Quorum Partition: %s Label: %s\n",
		       ctx.qc_device, ctx.qc_label);
	} else if (ctx.qc_device) {
		if (check_device(ctx.qc_device, NULL, NULL) != 0) {
			clulog(LOG_CRIT,
			       "Specified partition %s does not have a "
			       "qdisk label\n", ctx.qc_device);
			check_stop_cman(&ctx);
			return -1;
		}
	}

	if (!foreground && !forked) {
                if (daemon_init(argv[0]) < 0)
			return -1;
	}
	
	set_priority(ctx.qc_sched, ctx.qc_sched_prio);

	if (quorum_init(&ctx, ni, MAX_NODES_DISK, h, cfh) < 0) {
		clulog_and_print(LOG_CRIT, "Initialization failed\n");
		check_stop_cman(&ctx);
		return -1;
	}
	
	cman_register_quorum_device(ctx.qc_ch, ctx.qc_device, ctx.qc_votes);
	/*
		XXX this always returns -1 / EBUSY even when it works?!!!
		
	if ((rv = cman_register_quorum_device(ctx.qc_ch, ctx.qc_device,
					      ctx.qc_votes)) < 0) {
		clulog_and_print(LOG_CRIT,
				 "Could not register %s with CMAN; "
				 "return = %d; error = %s\n",
				 ctx.qc_device, rv, strerror(errno));
		return -1;
	}
	*/

	if (quorum_loop(&ctx, ni, MAX_NODES_DISK) == 0)
		cman_unregister_quorum_device(ctx.qc_ch);

	quorum_logout(&ctx);
	qd_destroy(&ctx);

	return 0;
}

