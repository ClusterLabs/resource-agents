#ifndef _LIBGROUP_H_
#define _LIBGROUP_H_

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_GROUP_MEMBERS	256
#define MAX_GROUP_NAME_LEN	32

/* these need to match what's in groupd.h */
#define GROUP_NODE_FAILED	1
#define GROUP_NODE_JOIN		2
#define GROUP_NODE_LEAVE	3

typedef void *group_handle_t;

typedef void (*group_stop_t)(group_handle_t h, void *priv, char *name);
typedef void (*group_start_t)(group_handle_t h, void *priv, char *name,
			      int event_nr, int type, int member_count,
			      int *members);
typedef void (*group_finish_t)(group_handle_t h, void *priv, char *name,
			       int event_nr);
typedef void (*group_terminate_t)(group_handle_t h, void *priv, char *name);
typedef void (*group_set_id_t)(group_handle_t h, void *priv, char *name,
			       unsigned int id);
typedef void (*group_deliver_t)(group_handle_t h, void *priv, char *name,
			        int nodeid, int len, char *buf);

typedef struct {
	group_stop_t stop;
	group_start_t start;
	group_finish_t finish;
	group_terminate_t terminate;
	group_set_id_t set_id;
	group_deliver_t deliver;
} group_callbacks_t;

group_handle_t group_init(void *priv, char *prog_name, int level, group_callbacks_t *cbs, int timeout);
int group_exit(group_handle_t handle);

int group_join(group_handle_t handle, char *name);
int group_leave(group_handle_t handle, char *name);
int group_stop_done(group_handle_t handle, char *name);
int group_start_done(group_handle_t handle, char *name, int event_nr);
int group_get_fd(group_handle_t handle);
int group_dispatch(group_handle_t handle);
int group_send(group_handle_t handle, char *name, int len, char *buf);


/*
 * Querying for group information
 */

typedef struct group_data {
	char client_name[32+1];
	char name[MAX_GROUP_NAME_LEN+1];
	int level;
	unsigned int id;
	int member;
	int member_count;
	int members[MAX_GROUP_MEMBERS];
	int event_state;
	int event_nodeid;
	int event_local_status;
	uint64_t event_id;
} group_data_t;

/* These routines create their own temporary connection to groupd so they
   don't interfere with dispatchable callback messages. */

int group_get_groups(int max, int *count, group_data_t *data);
int group_get_group(int level, const char *name, group_data_t *data);

int group_get_version(int *version);

#ifdef __cplusplus
}
#endif

#endif
