#ifndef __GD_INTERNAL_DOT_H__
#define __GD_INTERNAL_DOT_H__

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <dirent.h>
#include <syslog.h>
#include <time.h>
#include <sched.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <corosync/cpg.h>
#include <corosync/engine/logsys.h>

#include "list.h"
#include "linux_endian.h"
#include "groupd.h"
#include "libgroup.h"

#define MAX_NAMELEN		32	/* should match libgroup.h */
#define MAX_LEVELS		4
#define MAX_NODES		128

extern int daemon_debug_opt;
extern int daemon_debug_verbose;
extern int daemon_quit;
extern int cman_quorate;
extern int our_nodeid;
extern char *our_name;
extern char daemon_debug_buf[256];
extern char dump_buf[GROUPD_DUMP_SIZE];
extern int dump_point;
extern int dump_wrap;
extern struct list_head gd_groups;
extern struct list_head gd_levels[MAX_LEVELS];
extern uint32_t gd_event_nr;

#define GROUP_PENDING		1
#define GROUP_LIBGROUP          2
#define GROUP_LIBCPG            3

extern int group_mode;

#define DEFAULT_GROUPD_COMPAT		2
#define DEFAULT_GROUPD_WAIT		5
#define DEFAULT_GROUPD_MODE_DELAY	2
#define DEFAULT_DEBUG_LOGSYS		0

extern int optd_groupd_compat;
extern int optd_groupd_wait;
extern int optd_groupd_mode_delay;
extern int optd_debug_logsys;

extern int cfgd_groupd_compat;
extern int cfgd_groupd_wait;
extern int cfgd_groupd_mode_delay;
extern int cfgd_debug_logsys;

void daemon_dump_save(void);

#define log_debug(fmt, args...) \
do { \
	snprintf(daemon_debug_buf, 255, "%ld " fmt "\n", time(NULL), ##args); \
	daemon_dump_save(); \
	if (daemon_debug_opt) \
		fprintf(stderr, "%s", daemon_debug_buf); \
	if (cfgd_debug_logsys) \
		log_printf(LOG_DEBUG, "%s", daemon_debug_buf); \
} while (0)

#define log_group(g, fmt, args...) \
do { \
	snprintf(daemon_debug_buf, 255, "%ld %d:%s " fmt "\n", time(NULL), \
		 (g)->level, (g)->name, ##args); \
	daemon_dump_save(); \
	if (daemon_debug_opt) \
		fprintf(stderr, "%s", daemon_debug_buf); \
	if (cfgd_debug_logsys) \
		log_printf(LOG_DEBUG, "%s", daemon_debug_buf); \
} while (0)

#define log_print(fmt, args...) \
do { \
	log_debug(fmt, ##args); \
	log_printf(LOG_ERR, fmt, ##args); \
} while (0)

#define log_error(g, fmt, args...) \
do { \
	log_group(g, fmt, ##args); \
	log_printf(LOG_ERR, fmt, ##args); \
} while (0)

#define ASSERT(x) \
do { \
	if (!(x)) { \
		log_print("Assertion failed on line %d of file %s\n" \
			  "Assertion:  \"%s\"\n", __LINE__, __FILE__, #x); \
	} \
} while (0)

#ifndef TRUE
#define TRUE (1)
#endif
#ifndef FALSE
#define FALSE (0)
#endif

struct group;
struct app;
struct event;
struct node;
struct msg;
typedef struct group group_t;
typedef struct app app_t;
typedef struct event event_t;
typedef struct node node_t;
typedef struct msg msg_t;


/*
 * Event - manages nodes joining/leaving/failing
 */

#define EST_JOIN_BEGIN         1
#define EST_JOIN_STOP_WAIT     2
#define EST_JOIN_ALL_STOPPED   3
#define EST_JOIN_START_WAIT    4
#define EST_JOIN_ALL_STARTED   5
#define EST_LEAVE_BEGIN        6
#define EST_LEAVE_STOP_WAIT    7
#define EST_LEAVE_ALL_STOPPED  8
#define EST_LEAVE_START_WAIT   9
#define EST_LEAVE_ALL_STARTED 10
#define EST_FAIL_BEGIN        11
#define EST_FAIL_STOP_WAIT    12
#define EST_FAIL_ALL_STOPPED  13
#define EST_FAIL_START_WAIT   14
#define EST_FAIL_ALL_STARTED  15

struct event {
	struct list_head 	list;
	struct list_head	memb;
	int			event_nr;
	int 			state;
	int			nodeid;
	uint64_t		id;
	struct list_head	extended;
	int			start_app_before_pending_rev;
	int			fail_all_stopped;
};

/*
 * Group
 */

struct group {
	struct list_head 	list;		/* list of groups */
	struct list_head	level_list;
	uint16_t 		level;
	uint32_t		global_id;
	struct list_head 	memb;
	int 			memb_count;
	int 			namelen;
	char 			name[MAX_NAMELEN+1];
	app_t 			*app;
	struct list_head  	messages;
	cpg_handle_t		cpg_handle;
	int			cpg_fd;
	int			cpg_client;
	int			have_set_id;
	int			joining;
	int			leaving;
};

struct app {
	int			client;
	int 			node_count;
	struct list_head 	nodes;
	struct list_head	events;
	event_t			*current_event;
	group_t			*g;
	int			need_first_event; /* for debugging */
};

#define MSG_APP_STOPPED        1
#define MSG_APP_STARTED        2
#define MSG_APP_RECOVER        3
#define MSG_APP_INTERNAL       4
#define MSG_GLOBAL_ID          5
#define MSG_GROUP_VERSION      6

#define MSG_VER_MAJOR          1
#define MSG_VER_MINOR          1
#define MSG_VER_PATCH          0

struct msg {
	uint32_t		ms_version[3];
	uint16_t 		ms_type;
	uint16_t		ms_level;
	uint32_t 		ms_length;
	uint32_t 		ms_global_id;
	uint64_t		ms_event_id;
	char			ms_name[MAX_NAMELEN];
};

struct save_msg {
	struct list_head	list;
	int			nodeid;
	int			print_ignore;
	int			msg_len;
	msg_t			msg;
	char			*msg_long;
};

struct node {
	struct list_head 	list;
	int			nodeid;
	int			stopped;
	int			started;
};

struct recovery_set {
	struct list_head	list;
	struct list_head	entries;
	int			nodeid;
	int			cman_update;
	int			cpg_update;
};

struct recovery_entry {
	struct list_head	list;
	group_t			*group;
	int			recovered;
};


/* app.c */
void add_recovery_set_cman(int nodeid);
struct recovery_set *add_recovery_set_cpg(int nodeid, int procdown);
int queue_app_recover(group_t *g, int nodeid);
int queue_app_join(group_t *g, int nodeid);
int queue_app_leave(group_t *g, int nodeid);
int queue_app_message(group_t *g, struct save_msg *save);
int do_stopdone(char *name, int level);
int do_startdone(char *name, int level, int event_nr);
char *ev_state_str(event_t *ev);
event_t *find_queued_recover_event(group_t *g);
void extend_recover_event(group_t *g, event_t *ev, int nodeid);
int process_apps(void);
void del_event_nodes(event_t *ev);
void dump_group(group_t *g);
void dump_all_groups(void);
node_t *find_app_node(app_t *a, int nodeid);
int event_state_stopping(app_t *a);
int event_state_starting(app_t *a);
void msg_bswap_out(msg_t *msg);
void msg_bswap_in(msg_t *msg);
struct recovery_set *get_recovery_set(int nodeid);
void groupd_down(int nodeid);
char *msg_type(int type);
int process_app(group_t *g);
int is_our_join(event_t *ev);
void purge_node_messages(group_t *g, int nodeid);

/* main.c */
void read_ccs_name(char *path, char *name);
void read_ccs_yesno(char *path, int *yes, int *no);
void read_ccs_int(char *path, int *config_val);
void app_stop(app_t *a);
void app_setid(app_t *a);
void app_start(app_t *a);
void app_finish(app_t *a);
void app_terminate(app_t *a);
void app_deliver(app_t *a, struct save_msg *save);
int client_add(int fd, void (*workfn)(int ci), void (*deadfn)(int ci));
void client_dead(int ci);
void cluster_dead(int ci);

/* cman.c */
int setup_cman(void);
void close_cman(void);
void process_cman(int ci);
int kill_cman(int nodeid);

/* cpg.c */
int setup_cpg(void);
int do_cpg_join(group_t *g);
int do_cpg_leave(group_t *g);
int send_message(group_t *g, void *buf, int len);
int send_message_groupd(group_t *g, void *buf, int len, int type);
void copy_groupd_data(group_data_t *data);
int in_groupd_cpg(int nodeid);
void group_mode_check_timeout(void);

/* joinleave.c */
void remove_group(group_t *g);
int do_join(char *name, int level, int ci);
int do_leave(char *name, int level);
node_t *new_node(int nodeid);
group_t *find_group_level(char *name, int level);
int create_group(char *name, int level, group_t **g_out);
app_t *create_app(group_t *g);

/* logging.c */

void init_logging(void);
void setup_logging();
void close_logging(void);

#endif				/* __GD_INTERNAL_DOT_H__ */

