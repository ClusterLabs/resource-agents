#include <resgroup.h>
#include <rg_locks.h>
#include <gettid.h>
#include <assert.h>
#include <libcman.h>
#include <ccs.h>
#include <logging.h>
#include <lock.h>
#include <event.h>
#include <stdint.h>
#include <vf.h>
#include <members.h>
#include <time.h>


/**
 * resource group event queue.
 */
static event_t *event_queue = NULL;
#ifdef WRAP_LOCKS
static pthread_mutex_t event_queue_mutex = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;
static pthread_mutex_t mi_mutex = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;
#else
static pthread_mutex_t event_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mi_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif
static pthread_cond_t event_queue_cond = PTHREAD_COND_INITIALIZER;
static pthread_t event_thread = 0;
static int transition_throttling = 5;
static int central_events = 0;

extern int running;
extern int shutdown_pending;
static int _master = 0;
static struct dlm_lksb _master_lock;
static int _xid = 0;
static event_master_t *mi = NULL;

void hard_exit(void);
void flag_shutdown(int sig);
void flag_reconfigure(int sig);

event_table_t *master_event_table = NULL;


void
set_transition_throttling(int nsecs)
{
	if (nsecs < 0)
		nsecs = 0;
	transition_throttling = nsecs;
}


void
set_central_events(int flag)
{
	central_events = flag;
}


int
central_events_enabled(void)
{
	return central_events;
}


/**
  Called to handle the transition of a cluster member from up->down or
  down->up.  This handles initializing services (in the local node-up case),
  exiting due to loss of quorum (local node-down), and service fail-over
  (remote node down).  This is the distributed node event processor;
  for the local-only node event processor, see slang_event.c
 
  @param nodeID		ID of the member which has come up/gone down.
  @param nodeStatus		New state of the member in question.
  @see eval_groups
 */
void
node_event(int local, int nodeID, int nodeStatus,
	   int __attribute__((unused)) clean)
{
	if (!running)
		return;

	if (local) {

		/* Local Node Event */
		if (nodeStatus == 0) {
			log_printf(LOG_ERR, "Exiting uncleanly\n");
			hard_exit();
		}

		if (!rg_initialized()) {
			if (init_resource_groups(0, 0) != 0) {
				log_printf(LOG_ERR,
				       "#36: Cannot initialize services\n");
				hard_exit();
			}
		}

		if (shutdown_pending) {
			log_printf(LOG_NOTICE, "Processing delayed exit signal\n");
			running = 0;
			return;
		}
		setup_signal(SIGINT, flag_shutdown);
		setup_signal(SIGTERM, flag_shutdown);
		setup_signal(SIGHUP, flag_reconfigure);

		eval_groups(1, nodeID, 1);
		return;
	}

	/*
	 * Nothing to do for events from other nodes if we are not ready.
	 */
	if (!rg_initialized()) {
		log_printf(LOG_DEBUG, "Services not initialized.\n");
		return;
	}

	eval_groups(0, nodeID, nodeStatus);
}


/**
   Query CCS to see whether a node has fencing enabled or not in
   the configuration.  This does not check to see if it's in the
   fence domain.
 */
int
node_has_fencing(int nodeid)
{
	int ccs_desc;
	char *val = NULL;
	char buf[1024];
	int ret = 1;
	
	ccs_desc = ccs_connect();
	if (ccs_desc < 0) {
		log_printf(LOG_ERR, "Unable to connect to ccsd; cannot handle"
		       " node event!\n");
		/* Assume node has fencing */
		return 1;
	}

	snprintf(buf, sizeof(buf), 
		 "/cluster/clusternodes/clusternode[@nodeid=\"%d\"]"
		 "/fence/method/device/@name", nodeid);

	if (ccs_get(ccs_desc, buf, &val) != 0)
		ret = 0;
	if (val) 
		free(val);
	ccs_disconnect(ccs_desc);
	return ret;
}


/* Since the API for groupd is private, use group_tool to find
   out if we've joined the fence domain */ 
int
fence_domain_joined(void)
{
	int rv;

	rv = system("group_tool ls fence default &> /dev/null");	
	if (rv == 0)
		return 1;
	return 0;
}


/**
   Quick query to cman to see if a node has been fenced.
 */
int
node_fenced(int nodeid)
{
	cman_handle_t ch;
	int fenced = 0;
	uint64_t fence_time;

	ch = cman_init(NULL);
	if (cman_get_fenceinfo(ch, nodeid, &fence_time, &fenced, NULL) < 0)
		fenced = 0;

	cman_finish(ch);

	return fenced;
}


/**
   Callback from view-formation when a commit occurs for the Transition-
   Master key.
 */
int32_t
master_event_callback(char __attribute__ ((unused)) *key,
		      uint64_t __attribute__ ((unused)) viewno,
		      void *data, uint32_t datalen)
{
	event_master_t *m;

	m = data;
	if (datalen != (uint32_t)sizeof(*m)) {
		log_printf(LOG_ERR, "%s: wrong size\n", __FUNCTION__);
		return 1;
	}

	swab_event_master_t(m);
	if (m->m_magic != EVENT_MASTER_MAGIC) {
		log_printf(LOG_ERR, "%s: wrong size\n", __FUNCTION__);
		return 1;
	}

	if (m->m_nodeid == (uint32_t)my_id())
		log_printf(LOG_DEBUG, "Master Commit: I am master\n");
	else 
		log_printf(LOG_DEBUG, "Master Commit: %d is master\n", m->m_nodeid);

	pthread_mutex_lock(&mi_mutex);
	if (mi)
		free(mi);
	mi = m;
	pthread_mutex_unlock(&mi_mutex);

	return 0;
}


/**
  Read the Transition-Master key from vf if it exists.  If it doesn't,
  attempt to become the transition-master.
 */
static int
find_master(void)
{
	event_master_t *masterinfo = NULL;
	void *data;
	uint32_t sz;
	cluster_member_list_t *m;
	uint64_t vn;
	int master_id = -1;

	m = member_list();
	if (vf_read(m, "Transition-Master", &vn,
		    (void **)(&data), &sz) < 0) {
		log_printf(LOG_ERR, "Unable to discover master"
		       " status\n");
		masterinfo = NULL;
	} else {
		masterinfo = (event_master_t *)data;
	}
	free_member_list(m);

	if (masterinfo && (sz >= sizeof(*masterinfo))) {
		swab_event_master_t(masterinfo);
		if (masterinfo->m_magic == EVENT_MASTER_MAGIC) {
			log_printf(LOG_DEBUG, "Master Locate: %d is master\n",
			       masterinfo->m_nodeid);
			pthread_mutex_lock(&mi_mutex);
			if (mi)
				free(mi);
			mi = masterinfo;
			pthread_mutex_unlock(&mi_mutex);
			master_id = masterinfo->m_nodeid;
		}
	}

	return master_id;
}


/**
  Return a copy of the cached event_master_t structure to the
  caller.
 */
int
event_master_info_cached(event_master_t *mi_out)
{
	if (!central_events || !mi_out) {
		errno = -EINVAL;
		return -1;
	}

	pthread_mutex_lock(&mi_mutex);
	if (!mi) {
		pthread_mutex_unlock(&mi_mutex);
		errno = -ENOENT;
		return -1;
	}

	memcpy(mi_out, mi, sizeof(*mi));
	pthread_mutex_unlock(&mi_mutex);
	return 0;
}


/**
  Return the node ID of the master.  If none exists, become
  the master and return our own node ID.
 */
int
event_master(void)
{
	cluster_member_list_t *m = NULL;
	event_master_t masterinfo;
	int master_id = -1;

	/* We hold this forever. */
	if (_master)
		return my_id();

	m = member_list();
	pthread_mutex_lock(&mi_mutex);

	if (mi) {
		master_id = mi->m_nodeid;
		pthread_mutex_unlock(&mi_mutex);
		if (memb_online(m, master_id)) {
			//log_printf(LOG_DEBUG, "%d is master\n", mi->m_nodeid);
			goto out;
		}
	}

	pthread_mutex_unlock(&mi_mutex);

	memset(&_master_lock, 0, sizeof(_master_lock));
	if (clu_lock(LKM_EXMODE, &_master_lock, LKF_NOQUEUE,
		     "Transition-Master") < 0) {
		/* not us, find out who is master */
		master_id = find_master();
		goto out;
	}

	if (_master_lock.sb_status != 0) {
		master_id = -1;
		goto out;
	}

	_master = 1;

	memset(&masterinfo, 0, sizeof(masterinfo));
	masterinfo.m_magic = EVENT_MASTER_MAGIC;
	masterinfo.m_nodeid = my_id();
	masterinfo.m_master_time = (uint64_t)time(NULL);
	swab_event_master_t(&masterinfo);

	if (vf_write(m, VFF_IGN_CONN_ERRORS | VFF_RETRY,
		     "Transition-Master", &masterinfo,
		     sizeof(masterinfo)) < 0) {
		log_printf(LOG_ERR, "Unable to advertise master"
		       " status to all nodes\n");
	}

	master_id = my_id();
out:
	free_member_list(m);
	return master_id;
}



void group_event(char *name, uint32_t state, int owner);

/**
  Event handling function.  This only stays around as long as
  events are on the queue.
 */
void *
_event_thread_f(void __attribute__ ((unused)) *arg)
{
	event_t *ev;
	struct timeval now;
	struct timespec expire;
	int notice = 0, count = 0;

	/* Event thread usually doesn't hang around.  When it's
   	   spawned, sleep for this many seconds in order to let
   	   some events queue up */
	if (transition_throttling && !central_events) {
		sleep(transition_throttling);
	}

	while (1) {
		pthread_mutex_lock(&event_queue_mutex);
		ev = event_queue;
		if (!ev && !central_events) {
			gettimeofday(&now, NULL);
			expire.tv_sec = now.tv_sec + 5;
			expire.tv_nsec = now.tv_usec * 1000;
			pthread_cond_timedwait(&event_queue_cond,
						&event_queue_mutex,
						&expire);
			ev = event_queue;
		}
		if (!ev)
			break; /* We're outta here */

		list_remove(&event_queue, ev);

		++count;
		pthread_mutex_unlock(&event_queue_mutex);

		if (ev->ev_type == EVENT_CONFIG) {
			/*
			log_printf(LOG_NOTICE, "Config Event: %d -> %d\n",
			       ev->ev.config.cfg_oldversion,
			       ev->ev.config.cfg_version);
			 */
			init_resource_groups(1, 0);
			free(ev);
			continue;
		}

		if (central_events) {
			/* If the master node died or there isn't
			   one yet, take the master lock. */
			if (event_master() == my_id()) {
				slang_process_event(master_event_table,
						    ev);
			} 
			free(ev);
			continue;
			/* ALL OF THE CODE BELOW IS DISABLED
			   when using central_events */
		}

		if (ev->ev_type == EVENT_RG) {
			/*
			log_printf(LOG_NOTICE, "RG Event: %s %s %d\n",
			       ev->ev.group.rg_name,
			       rg_state_str(ev->ev.group.rg_state),
			       ev->ev.group.rg_owner);
			 */
			group_event(ev->ev.group.rg_name,
				    ev->ev.group.rg_state,
				    ev->ev.group.rg_owner);
		} else if (ev->ev_type == EVENT_NODE) {
			/*
			log_printf(LOG_NOTICE, "Node Event: %s %d %s %s\n",
			       ev->ev.node.ne_local?"Local":"Remote",
			       ev->ev.node.ne_nodeid,
			       ev->ev.node.ne_state?"UP":"DOWN",
			       ev->ev.node.ne_clean?"Clean":"Dirty")
			 */

			if (ev->ev.node.ne_state == 0 &&
			    !ev->ev.node.ne_clean &&
			    node_has_fencing(ev->ev.node.ne_nodeid)) {
				notice = 0;
				while (!node_fenced(ev->ev.node.ne_nodeid)) {
					if (!notice) {
						notice = 1;
						log_printf(LOG_INFO, "Waiting for "
						       "node #%d to be fenced\n",
						       ev->ev.node.ne_nodeid);
					}
					sleep(2);
				}

				if (notice)
					log_printf(LOG_INFO, "Node #%d fenced; "
					       "continuing\n",
					       ev->ev.node.ne_nodeid);
			}

			node_event(ev->ev.node.ne_local,
				   ev->ev.node.ne_nodeid,
				   ev->ev.node.ne_state,
				   ev->ev.node.ne_clean);
		}

		free(ev);
	}

	if (!central_events || _master) {
		log_printf(LOG_DEBUG, "%d events processed\n", count);
	}
	/* Mutex held */
	event_thread = 0;
	pthread_mutex_unlock(&event_queue_mutex);
	pthread_exit(NULL);
}


static void
insert_event(event_t *ev)
{
	pthread_attr_t attrs;
	pthread_mutex_lock (&event_queue_mutex);
	ev->ev_transaction = ++_xid;
	list_insert(&event_queue, ev);
	if (event_thread == 0) {
        	pthread_attr_init(&attrs);
        	pthread_attr_setinheritsched(&attrs, PTHREAD_INHERIT_SCHED);
        	pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED);
		pthread_attr_setstacksize(&attrs, 262144);

		pthread_create(&event_thread, &attrs, _event_thread_f, NULL);
        	pthread_attr_destroy(&attrs);
	} else {
		pthread_cond_broadcast(&event_queue_cond);
	}
	pthread_mutex_unlock (&event_queue_mutex);
}


static event_t *
new_event(void)
{
	event_t *ev;

	while (1) {
		ev = malloc(sizeof(*ev));
		if (ev) {
			break;
		}
		sleep(1);
	}
	memset(ev,0,sizeof(*ev));
	ev->ev_type = EVENT_NONE;

	return ev;
}


void
rg_event_q(char *name, uint32_t state, int owner, int last)
{
	event_t *ev = new_event();

	ev->ev_type = EVENT_RG;

	strncpy(ev->ev.group.rg_name, name, 128);
	ev->ev.group.rg_state = state;
	ev->ev.group.rg_owner = owner;
	ev->ev.group.rg_last_owner = last;

	insert_event(ev);
}


void
node_event_q(int local, int nodeID, int state, int clean)
{
	event_t *ev = new_event();

	ev->ev_type = EVENT_NODE;
	ev->ev.node.ne_state = state;
	ev->ev.node.ne_local = local;
	ev->ev.node.ne_nodeid = nodeID;
	ev->ev.node.ne_clean = clean;
	insert_event(ev);
}


void
config_event_q(void)
{
	event_t *ev = new_event();

	ev->ev_type = EVENT_CONFIG;
	insert_event(ev);
}

void
user_event_q(char *svc, int request,
	     int arg1, int arg2, int target, msgctx_t *ctx)
{
	event_t *ev = new_event();

	ev->ev_type = EVENT_USER;
	strncpy(ev->ev.user.u_name, svc, sizeof(ev->ev.user.u_name));
	ev->ev.user.u_request = request;
	ev->ev.user.u_arg1 = arg1;
	ev->ev.user.u_arg2 = arg2;
	ev->ev.user.u_target = target;
	ev->ev.user.u_ctx = ctx;
	insert_event(ev);
}

