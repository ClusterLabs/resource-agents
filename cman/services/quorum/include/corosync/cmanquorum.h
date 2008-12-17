#ifndef COROSYNC_CMANQUORUM_H_DEFINED
#define COROSYNC_CMANQUORUM_H_DEFINED

typedef uint64_t cmanquorum_handle_t;


#define CMANQUORUM_MAX_QDISK_NAME_LEN 255

#define CMANQUORUM_INFO_FLAG_DIRTY      1
#define CMANQUORUM_INFO_FLAG_DISALLOWED 2
#define CMANQUORUM_INFO_FLAG_TWONODE    4
#define CMANQUORUM_INFO_FLAG_QUORATE    8

#define NODESTATE_JOINING    1
#define NODESTATE_MEMBER     2
#define NODESTATE_DEAD       3
#define NODESTATE_LEAVING    4
#define NODESTATE_DISALLOWED 5


/** @} */

struct cmanquorum_info {
	int node_id;
	unsigned int node_votes;
	unsigned int node_expected_votes;
	unsigned int highest_expected;
	unsigned int total_votes;
	unsigned int quorum;
	unsigned int flags;
};

struct cmanquorum_qdisk_info {
	int votes;
	int state;
	char name[CMANQUORUM_MAX_QDISK_NAME_LEN];
};

typedef struct {
	uint32_t nodeid;
	uint32_t state;
} cmanquorum_node_t;


typedef void (*cmanquorum_notification_fn_t) (
	cmanquorum_handle_t handle,
	uint32_t quorate,
	uint32_t node_list_entries,
	cmanquorum_node_t node_list[]
	);

typedef struct {
	cmanquorum_notification_fn_t cmanquorum_notify_fn;
} cmanquorum_callbacks_t;


/*
 * Create a new quorum connection
 */
cs_error_t cmanquorum_initialize (
	cmanquorum_handle_t *handle,
	cmanquorum_callbacks_t *callbacks);

/*
 * Close the quorum handle
 */
cs_error_t cmanquorum_finalize (
	cmanquorum_handle_t handle);


/*
 * Dispatch messages and configuration changes
 */
cs_error_t cmanquorum_dispatch (
	cmanquorum_handle_t handle,
	cs_dispatch_flags_t dispatch_types);


/*
 * Get quorum information.
 */
cs_error_t cmanquorum_getinfo (
	cmanquorum_handle_t handle,
	int nodeid,
	struct cmanquorum_info *info);

/*
 * set expected_votes
 */
cs_error_t cmanquorum_setexpected (
	cmanquorum_handle_t handle,
	unsigned int expected_votes);

/*
 * set votes for a node
 */
cs_error_t cmanquorum_setvotes (
	cmanquorum_handle_t handle,
	int nodeid,
	unsigned int votes);

/*
 * Register a quorum device
 * it will be DEAD until polled
 */
cs_error_t cmanquorum_qdisk_register (
	cmanquorum_handle_t handle,
	char *name,
	unsigned int votes);

/*
 * Unregister a quorum device
 */
cs_error_t cmanquorum_qdisk_unregister (
	cmanquorum_handle_t handle);

/*
 * Poll a quorum device
 */
cs_error_t cmanquorum_qdisk_poll (
	cmanquorum_handle_t handle,
	int state);

/*
 * Get quorum device information
 */
cs_error_t cmanquorum_qdisk_getinfo (
	cmanquorum_handle_t handle,
	struct cmanquorum_qdisk_info *info);

/*
 * Set the dirty bit for this node
 */
cs_error_t cmanquorum_setdirty (
	cmanquorum_handle_t handle);

/*
 * Force a node to exit
 */
cs_error_t cmanquorum_killnode (
	cmanquorum_handle_t handle,
	int nodeid,
	unsigned int reason);

/* Track node and quorum changes */
cs_error_t cmanquorum_trackstart (
	cmanquorum_handle_t handle,
	unsigned int flags );

cs_error_t cmanquorum_trackstop (
	cmanquorum_handle_t handle);

/*
 * Set our LEAVING flag. we should exit soon after this
 */
cs_error_t cmanquorum_leaving (
	cmanquorum_handle_t handle);

/*
 * Save and retrieve private data/context
 */
cs_error_t cmanquorum_context_get (
	cmanquorum_handle_t handle,
	void **context);

cs_error_t cmanquorum_context_set (
	cmanquorum_handle_t handle,
	void *context);

#endif /* COROSYNC_CMANQUORUM_H_DEFINED */
