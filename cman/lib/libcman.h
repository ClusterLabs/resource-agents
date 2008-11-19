#ifndef _LIBCMAN_H_
#define _LIBCMAN_H_

#include <netinet/in.h>

#define LIBCMAN_VERSION 3

/*
 * Some maxima
 */
#define CMAN_MAX_ADDR_LEN             sizeof(struct sockaddr_in6)
#define CMAN_MAX_NODENAME_LEN         255
#define MAX_CLUSTER_NAME_LEN           16
#define CMAN_MAX_FENCE_AGENT_NAME_LEN 255

/*
 * Pass this into cman_get_node() as the nodeid to get local node information
 */
#define CMAN_NODEID_US     0

/*
 * Pass this into cman_get_node() as the nodeid to get quorum device information
 */
#define CMAN_NODEID_QDISK -1


/* Pass this into cman_send_data to send a message to all nodes */
#define CMAN_NODEID_ALL 0

/*
 * Hang onto this, it's your key into the library. get one from cman_init() or
 * cman_admin_init()
 */
typedef void *cman_handle_t;

/*
 * Reasons we get an event callback.
 * PORTOPENED & TRY_SHUTDOWN only exist when LIBCMAN_VERSION >= 2
 *
 * The 'arg' parameter varies depending on the callback type.
 * for PORTCLOSED/PORTOPENED  arg == the port opened/closed
 * for STATECHANGE            arg is quorum state (1=quorate, 0=not)
 * for TRY_SHUTDOWN           arg == 1 for ANYWAY, otherwise 0 (ie if arg == 1 
 * 			      then cman WILL shutdown regardless
 *                            of your response, think of this as advance warning)
 * for CONFIG_UPDATE          arg will be the new config version
 */
typedef enum {CMAN_REASON_PORTCLOSED,
	      CMAN_REASON_STATECHANGE,
              CMAN_REASON_PORTOPENED,
              CMAN_REASON_TRY_SHUTDOWN,
              CMAN_REASON_CONFIG_UPDATE} cman_call_reason_t;

/*
 * Reason flags for cman_leave
 */
#define CMAN_LEAVEFLAG_DOWN    0
#define CMAN_LEAVEFLAG_REMOVED 3
#define CMAN_LEAVEFLAG_FORCE   0x10

/*
 * Flags for cman_shutdown
 *    ANYWAY   -  cman will shutdown regardless of clients' responses (but they
 *    		   will still get told)
 *    REMOVED  -  the rest of the cluster will adjust quorum to stay quorate
 */
#define CMAN_SHUTDOWN_ANYWAY   1
#define CMAN_SHUTDOWN_REMOVED  2

/*
 * Flags passed to cman_dispatch():
 * CMAN_DISPATCH_ONE dispatches a single message then returns,
 * CMAN_DISPATCH_ALL dispatches all outstanding messages (ie till EAGAIN) then 
 *                   returns,
 * CMAN_DISPATCH_BLOCKING forces it to wait for a message (clears MSG_DONTWAIT 
 * 			  in recvmsg)
 * CMAN_DISPATCH_IGNORE_* allows the caller to select which messages to process.
 */
#define CMAN_DISPATCH_ONE           0
#define CMAN_DISPATCH_ALL           1
#define CMAN_DISPATCH_BLOCKING      2
#define CMAN_DISPATCH_IGNORE_REPLY  4
#define CMAN_DISPATCH_IGNORE_DATA   8
#define CMAN_DISPATCH_IGNORE_EVENT 16
#define CMAN_DISPATCH_TYPE_MASK     3
#define CMAN_DISPATCH_IGNORE_MASK  46

/*
 * A node address. This is a complete sockaddr_in[6]
 * To explain:
 *  If you cast cna_address to a 'struct sockaddr', the sa_family field
 *  will be AF_INET or AF_INET6. Armed with that knowledge you can then
 *  cast it to a sockaddr_in or sockaddr_in6 and pull out the address.
 *  No other sockaddr fields are valid.
 *  Also, you must ignore any part of the sockaddr beyond the length supplied
 */
typedef struct cman_node_address
{
	int  cna_addrlen;
	char cna_address[CMAN_MAX_ADDR_LEN];
} cman_node_address_t;

/*
 * Return from cman_get_node()
 */
typedef struct cman_node
{
	int cn_nodeid;
	cman_node_address_t cn_address;
	char cn_name[CMAN_MAX_NODENAME_LEN+1];
	int cn_member;
	int cn_incarnation;
	struct timeval cn_jointime;
} cman_node_t;

/*
 * Returned from cman_get_version(),
 * input to cman_set_version(), though only cv_config can be changed
 */
typedef struct cman_version
{
	unsigned int cv_major;
	unsigned int cv_minor;
	unsigned int cv_patch;
	unsigned int cv_config;
} cman_version_t;

/*
 * Return from cman_get_cluster()
 */
typedef struct cman_cluster
{
	char     ci_name[MAX_CLUSTER_NAME_LEN+1];
	uint16_t ci_number;
	uint32_t ci_generation;
} cman_cluster_t;

/*
 * This is returned from cman_get_extra_info - it's really
 * only for use by cman_tool, don't depend on this not changing
 */

/* Flags in ei_flags */
#define CMAN_EXTRA_FLAG_2NODE      1
#define CMAN_EXTRA_FLAG_ERROR      2
#define CMAN_EXTRA_FLAG_SHUTDOWN   4
#define CMAN_EXTRA_FLAG_DISALLOWED 8
#define CMAN_EXTRA_FLAG_DIRTY     16

typedef struct cman_extra_info {
	int           ei_node_state;
	int           ei_flags;
	int           ei_node_votes;
	int           ei_total_votes;
	int           ei_expected_votes;
	int           ei_quorum;
	int           ei_members;
	char          ei_ports[32];
	int           ei_num_addresses;
	char          ei_addresses[1]; /* Array of num_addresses*sockaddr_storage*2
					  First batch is the multicast address list */
} cman_extra_info_t;

/* Quorum device info, returned from cman_get_quorum_device() */
typedef struct cman_qdev_info {
	char qi_name[CMAN_MAX_NODENAME_LEN+1];
	int  qi_state;
	int  qi_votes;
} cman_qdev_info_t;

/*
 * NOTE: Apart from cman_replyto_shutdown(), you must not
 * call other cman_* functions while in these two callbacks:
 */

/* Callback routine for a membership or other event */
typedef void (*cman_callback_t)(cman_handle_t handle, void *privdata, int reason, int arg);

/* Callback routine for data received */
typedef void (*cman_datacallback_t)(cman_handle_t handle, void *privdata,
				    char *buf, int len, uint8_t port, int nodeid);

/* Callback for nodes joining/leaving */
typedef void (*cman_confchgcallback_t)(cman_handle_t handle, void *privdata,
				       unsigned int *member_list, int member_list_entries,
				       unsigned int *left_list, int left_list_entries,
				       unsigned int *joined_list, int joined_list_entries);

/*
 * cman_init        returns the handle you need to pass to the other API calls.
 * cman_admin_init  opens admin socket for privileged operations.
 * cman_finish      destroys that handle.
 *
 * Note that admin sockets can't send data messages or receive callbacks.
 */
cman_handle_t cman_init(void *privdata);
cman_handle_t cman_admin_init(void *privdata);
int cman_finish(cman_handle_t handle);

/* Update/retrieve the private data */
int cman_setprivdata(cman_handle_t h, void *privdata);
int cman_getprivdata(cman_handle_t h, void **privdata);

/*
 * Notification of membership change events. Note that these are sent after
 * a transition, so multiple nodes may have left or joined the cluster.
 */
int cman_start_notification(cman_handle_t handle, cman_callback_t callback);
int cman_stop_notification(cman_handle_t handle);

/*
 * Start/stop AIS-style confchg callbacks. These are less racy than the
 * old cman callbacks in that the caller will get one for each AIS
 * confchg message and it will contain all of the nodes that joined &
 * left in that transition.
 */
int cman_start_confchg(cman_handle_t handle, cman_confchgcallback_t callback);
int cman_stop_confchg(cman_handle_t handle);

/* Call this if you get a TRY_SHUTDOWN event to signal whether you
 * will let cman shutdown or not.
 * Note that getting this callback does not mean that cman WILL shutdown,
 * only that it might. To detect a cman shutdown see cman_dispatch() below.
 */
int cman_replyto_shutdown(cman_handle_t, int yesno);


/*
 * Get the internal CMAN fd so you can pass it into poll() or select().
 * When it's active then call cman_dispatch() on the handle to process the event
 * NOTE: This fd can change between calls to cman_dispatch() so always call this
 * routine to get the latest one. (This is mainly due to message caching).
 * One upshot of this is that you must never read or write this FD (it may on 
 * occasion point to /dev/zero if you have messages cached!)
 */
int cman_get_fd(cman_handle_t handle);

/*
 * cman_dispatch() will return -1 with errno == EHOSTDOWN if the cluster is
 * shut down, 0 if nothing was read, or a positive number if something was 
 * dispatched.
 */

int cman_dispatch(cman_handle_t handle, int flags);


/*
 * -----------------------------------------------------------------------------
 * Get info calls.
 */

/* Return the number of nodes we know about. This will normally
 *  be the number of nodes in CCS 
 */
int cman_get_node_count(cman_handle_t handle);

/* Returns the number of connected clients. This isn't as useful as a it used to
 * be as a count >1 does not automatically mean cman won't shut down. Subsystems
 * can decide for themselves whether a clean shutdown is possible. 
 */
int cman_get_subsys_count(cman_handle_t handle);

/* Returns an array of node info structures. Call cman_get_node_count() first
 * to determine how big your array needs to be 
 */
int cman_get_nodes(cman_handle_t handle, int maxnodes, int *retnodes, cman_node_t *nodes);

/* Returns a list of nodes that are known to AIS but blocked from joining the
 * CMAN cluster because they rejoined with cluster without a cman_tool join 
 */
int cman_get_disallowed_nodes(cman_handle_t handle, int maxnodes, int *retnodes, cman_node_t *nodes);

/*
 * cman_get_node() can get node info by nodeid OR by name. If the first
 * char of node->cn_name is zero then the nodeid will be used, otherwise
 * the name will be used. I'll say this differently: If you want to look
 * up a node by nodeid, you MUST clear out the cman_node_t structure passed
 * into cman_get_node(). nodeid can be CMAN_NODEID_US.
 */
int cman_get_node(cman_handle_t handle, int nodeid, cman_node_t *node);

/* cman_get_node() only returns the first address of a node (whatever /that/
 * may mean). If you want to know all of them you need to call this.
 * max_addrs is the size of the 'addrs' array. num_addrs will be filled in by 
 * the number of addresses the node has, regardless of the size of max_addrs. 
 * So if you don't allocate enough space for the first call, you should know how
 * much is needed for a second!
 */
int cman_get_node_addrs(cman_handle_t handle, int nodeid, int max_addrs, int *num_addrs, struct cman_node_address *addrs);

/* Returns 1 if cman has completed initialisation and aisexec is running */
int cman_is_active(cman_handle_t handle);

/*
 * Returns 1 if a client is registered for data callbacks on a particular
 * port on a particular node. if cman returns -1 (errno==EBUSY) then it
 * doesn't currently know the status but has requested it, so try again
 * later or wait for a PORTOPENED notification.
 * nodeid can be CMAN_NODEID_US
 */
int cman_is_listening(cman_handle_t handle, int nodeid, uint8_t port);

/* Do we have quorum? */
int cman_is_quorate(cman_handle_t handle);

/* Return software & config (cluster.conf file) version */
int cman_get_version(cman_handle_t handle, cman_version_t *version);

/* Get cluster name and number */
int cman_get_cluster(cman_handle_t handle, cman_cluster_t *clinfo);

/* Get fence information for a node.
 * 'int *fenced' is only valid if the node is down, it is set to
 * 1 if the node has been fenced since it left the cluster.
 * agent should be CMAN_MAX_FENCE_AGENT_NAME_LEN
 */
int cman_get_fenceinfo(cman_handle_t handle, int nodeid, uint64_t *fence_time, int *fenced, char *agent);

/* Get stuff for cman_tool. Nobody else should use this */
int cman_get_extra_info(cman_handle_t handle, cman_extra_info_t *info, int maxlen);

/* Dump the objdb contents (only works if compiled with DEBUG enabled) */
int cman_dump_objdb(cman_handle_t handle, char *filename);

/*
 * -----------------------------------------------------------------------------
 * Admin functions. You will need privileges and have a handle created by 
 * cman_admin_init() to use them.
 */

/* Change the config file version. This should be needed much less now, as 
 * cman will re-read the config file if a new node joins with a new config 
 * version */
int cman_set_version(cman_handle_t handle, const cman_version_t *version);

/* Deprecated in favour of cman_shutdown(). Use cman_tool anyway please. */
int cman_leave_cluster(cman_handle_t handle, int reason);

/* Change the number of votes for this node. NOTE: a CCS update will
   overwrite this, so make sure you change both. Or, better, change CCS
   and call set_version() */
int cman_set_votes(cman_handle_t handle, int votes, int nodeid);

/* As above, for expected_votes */
int cman_set_expected_votes(cman_handle_t handle, int expected_votes);

/* Tell a particular node to leave the cluster NOW */
int cman_kill_node(cman_handle_t handle, int nodeid);

/* Tell CMAN a node has been fenced, when and by what means. */
int cman_node_fenced(cman_handle_t handle, int nodeid, uint64_t fence_time, char *agent);

/*
 * cman_shutdown() will send a REASON_TRY_SHUTDOWN event to all
 * clients registered for notifications. They should respond by calling
 * cman_replyto_shutdown() to indicate whether they will allow
 * cman to close down or not. If cman gets >=1 "no" (0) replies or the
 * request times out (default 5 seconds) then shutdown will be
 * cancelled and cman_shutdown() will return -1 with errno == EBUSY.
 *
 * Set flags to CMAN_SHUTDOWN_ANYWAY to force shutdown. Clients will still
 * be notified /and/ they will know you want a forced shutdown.
 *
 * Setting flags to CMAN_SHUTDOWN_REMOVED will tell the rest of the
 * cluster to adjust quorum to keep running with this node has left
 */
int cman_shutdown(cman_handle_t, int flags);

/* -----------------------------------------------------------------------------
 * Data transmission API. Uses the same FD as the rest of the calls.
 * If the nodeid passed to cman_send_data() is zero then it will be
 * broadcast to all nodes in the cluster.
 * cman_start_recv_data() is like a bind(), and marks the port
 * as "listening". See cman_is_listening() above.
 */
int cman_send_data(cman_handle_t handle, const void *buf, int len, int flags, uint8_t port, int nodeid);
int cman_start_recv_data(cman_handle_t handle, cman_datacallback_t, uint8_t port);
int cman_end_recv_data(cman_handle_t handle);

/*
 * Barrier API.
 * Here for backwards compatibility. Most of the things you would achieve
 * with this can now be better done using openAIS services or just messaging.
 */
int cman_barrier_register(cman_handle_t handle, const char *name, int flags, int nodes);
int cman_barrier_change(cman_handle_t handle, const char *name, int flags, int arg);
int cman_barrier_wait(cman_handle_t handle, const char *name);
int cman_barrier_delete(cman_handle_t handle, const char *name);

/*
 * Add your own quorum device here, needs an admin socket
 *
 * After creating a quorum device you will need to call 'poll_quorum_device'
 * at least once every (default) 10 seconds (this can be changed in CCS)
 * otherwise it will time-out and the cluster will lose its vote.
 */
int cman_register_quorum_device(cman_handle_t handle, char *name, int votes);
int cman_unregister_quorum_device(cman_handle_t handle);
int cman_poll_quorum_device(cman_handle_t handle, int isavailable);
int cman_get_quorum_device(cman_handle_t handle, struct cman_qdev_info *info);

/*
 * Sets the dirty bit inside cman. This indicates that the node has
 * some internal 'state' (eg in a daemon, filesystem or lock manager)
 * and cannot merge with another cluster that already has state.
 * This needs an admin socket. It cannot be reset. 
 */
int cman_set_dirty(cman_handle_t handle);


/*
 * Changes the debug logging level inside cman.
 * subsystems is a bitmask of:
 */
#define CMAN_DEBUGLOG_NONE       0
#define CMAN_DEBUGLOG_BARRIER    2
#define CMAN_DEBUGLOG_MEMBERSHIP 4
#define CMAN_DEBUGLOG_DAEMON     8
#define CMAN_DEBUGLOG_AIS       16

int cman_set_debuglog(cman_handle_t handle, int subsystems);

#endif
