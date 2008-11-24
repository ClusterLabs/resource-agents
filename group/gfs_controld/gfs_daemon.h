#ifndef __GFS_DAEMON_DOT_H__
#define __GFS_DAEMON_DOT_H__

#include <sys/types.h>
#include <asm/types.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/poll.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <fcntl.h>
#include <netdb.h>
#include <limits.h>
#include <unistd.h>
#include <time.h>
#include <syslog.h>
#include <sched.h>
#include <signal.h>
#include <sys/time.h>
#include <dirent.h>
#include <openais/saAis.h>
#include <openais/saCkpt.h>
#include <corosync/cpg.h>
#include <liblogthread.h>

#include <linux/dlmconstants.h>
#include "libgfscontrol.h"
#include "gfs_controld.h"
#include "list.h"
#include "linux_endian.h"

/* TODO: warn if
   DLM_LOCKSPACE_LEN (from dlmconstants.h) !=
   GFS_MOUNTGROUP_LEN (from libgfscontrol.h)
*/

/* Maximum members of a mountgroup, should match CPG_MEMBERS_MAX in
   corosync/cpg.h.  There are no max defines in gfs-kernel for
   mountgroup members. (FIXME verify gfs-kernel/lock_dlm) */

#define MAX_NODES       128

/* Max string length printed on a line, for debugging/dump output. */

#define MAXLINE         256

/* group_mode */

#define GROUP_LIBGROUP	2
#define GROUP_LIBCPG	3

extern int daemon_debug_opt;
extern int daemon_quit;
extern int cluster_down;
extern int poll_dlm;
extern int poll_ignore_plock;
extern int plock_fd;
extern int plock_ci;
extern struct list_head mountgroups;
extern int cman_quorate;
extern int our_nodeid;
extern char *clustername;
extern char daemon_debug_buf[256];
extern char dump_buf[GFSC_DUMP_SIZE];
extern int dump_point;
extern int dump_wrap;
extern char plock_dump_buf[GFSC_DUMP_SIZE];
extern int plock_dump_len;
extern int dmsetup_wait;
extern cpg_handle_t cpg_handle_daemon;
extern int libcpg_flow_control_on;
extern int group_mode;
extern uint32_t plock_minor;
extern uint32_t old_plock_minor;
extern struct list_head withdrawn_mounts;

void daemon_dump_save(void);

#define log_level(lvl, fmt, args...) \
do { \
	snprintf(daemon_debug_buf, 255, "%ld " fmt "\n", time(NULL), ##args); \
	daemon_dump_save(); \
	logt_print(lvl, fmt "\n", ##args); \
	if (daemon_debug_opt) \
		fprintf(stderr, "%s", daemon_debug_buf); \
} while (0)

#define log_debug(fmt, args...) log_level(LOG_DEBUG, fmt, ##args)
#define log_error(fmt, args...) log_level(LOG_ERR, fmt, ##args)

#define log_group(g, fmt, args...) \
do { \
	snprintf(daemon_debug_buf, 255, "%ld %s " fmt "\n", time(NULL), \
		 (g)->name, ##args); \
	daemon_dump_save(); \
	logt_print(LOG_DEBUG, fmt "\n", ##args); \
	if (daemon_debug_opt) \
		fprintf(stderr, "%s", daemon_debug_buf); \
} while (0)

#define log_plock(g, fmt, args...) \
do { \
	snprintf(daemon_debug_buf, 255, "%ld %s " fmt "\n", time(NULL), \
		 (g)->name, ##args); \
	if (daemon_debug_opt && cfgd_plock_debug) \
		fprintf(stderr, "%s", daemon_debug_buf); \
} while (0)

struct mountgroup {
	struct list_head	list;
	uint32_t		id;
	struct gfsc_mount_args	mount_args;
	char			name[GFS_MOUNTGROUP_LEN+1];
	int			old_group_mode;

	int			mount_client;
	int			mount_client_result;
	int			mount_client_notified;
	int			mount_client_delay;
	int			remount_client;

	int			withdraw_uevent;
	int			withdraw_suspend;
	int			dmsetup_wait;
	pid_t			dmsetup_pid;
	int			our_jid;
	int			spectator;
	int			ro;
	int			rw;
	int                     joining;
	int                     leaving;
	int			kernel_mount_error;
	int			kernel_mount_done;
	int			first_mounter;

	/* cpg-new stuff */

	cpg_handle_t            cpg_handle;
	int                     cpg_client;
	int                     cpg_fd;
	int                     kernel_stopped;
	uint32_t                change_seq;
	uint32_t                started_count;
	struct change           *started_change;
	struct list_head        changes;
	struct list_head        node_history;
	struct list_head	journals;
	int			dlm_notify_nodeid;
	int			first_done_uevent;
	int			first_recovery_needed;
	int			first_recovery_master;
	int			first_recovery_msg;
	int			local_recovery_jid;
	int			local_recovery_busy;

	/* cpg-old stuff for rhel5/stable2 compat */

	struct list_head	members;
	struct list_head	members_gone;
	int			memb_count;
	int			last_stop;
	int			last_start;
	int			last_finish;
	int			last_callback;
	int			start_event_nr;
	int			start_type;
	int                     group_leave_on_finish;
	int			init;
	int			got_our_options;
	int			got_our_journals;
	int			delay_send_journals;
	int			first_mount_pending_stop;
	int			first_mounter_done;
	int			global_first_recover_done;
	int			emulate_first_mounter;
	int			wait_first_done;
	int			needs_recovery;
	int			low_nodeid;
	int			master_nodeid;
	int			got_kernel_mount;
	struct list_head	saved_messages;
	void			*start2_fn;

	/* cpg-old plock stuff */

	int			save_plocks;
	struct list_head	plock_resources;
	uint32_t		associated_ls_id;
	uint64_t		cp_handle;
	time_t			last_checkpoint_time;
	time_t			last_plock_time;
	struct timeval		drop_resources_last;
};

/* these need to match the kernel defines of the same name in lm_interface.h */

#define LM_RD_GAVEUP 308
#define LM_RD_SUCCESS 309

/* config.c */
int setup_ccs(void);
void close_ccs(void);
void read_ccs_name(char *path, char *name);
void read_ccs_yesno(char *path, int *yes, int *no);
void read_ccs_int(char *path, int *config_val);
void read_ccs_nodir(struct mountgroup *mg, char *buf);

/* cpg-new.c */
int setup_cpg(void);
void close_cpg(void);
void process_cpg(int ci);
int setup_dlmcontrol(void);
void process_dlmcontrol(int ci);
int set_protocol(void);
void process_recovery_uevent(char *table);
void process_mountgroups(void);
int gfs_join_mountgroup(struct mountgroup *mg);
void do_leave(char *table, int mnterr);
void gfs_mount_done(struct mountgroup *mg);
void send_remount(struct mountgroup *mg, struct gfsc_mount_args *ma);
void send_withdraw(struct mountgroup *mg);
int set_mountgroup_info(struct mountgroup *mg, struct gfsc_mountgroup *out);
int set_node_info(struct mountgroup *mg, int nodeid, struct gfsc_node *node);
int set_mountgroups(int *count, struct gfsc_mountgroup **mgs_out);
int set_mountgroup_nodes(struct mountgroup *mg, int option, int *node_count,
	struct gfsc_node **nodes_out);
void free_mg(struct mountgroup *mg);

/* cpg-old.c */
int setup_cpg_old(void);
void close_cpg_old(void);
void process_cpg_old(int ci);

int gfs_join_mountgroup_old(struct mountgroup *mg, struct gfsc_mount_args *ma);
void do_leave_old(char *table, int mnterr);
int send_group_message_old(struct mountgroup *mg, int len, char *buf);
void save_message_old(struct mountgroup *mg, char *buf, int len, int from,
	int type);
void send_withdraw_old(struct mountgroup *mg);
int process_recovery_uevent_old(char *table);
void ping_kernel_mount_old(char *table);
void send_remount_old(struct mountgroup *mg, struct gfsc_mount_args *ma);
void send_mount_status_old(struct mountgroup *mg);
int do_stop(struct mountgroup *mg);
int do_finish(struct mountgroup *mg);
void do_start(struct mountgroup *mg, int type, int member_count, int *nodeids);
int do_terminate(struct mountgroup *mg);
int do_withdraw_old(char *table);

/* group.c */
int setup_groupd(void);
void close_groupd(void);
void process_groupd(int ci);
int set_mountgroup_info_group(struct mountgroup *mg,
	struct gfsc_mountgroup *out);
int set_node_info_group(struct mountgroup *mg, int nodeid,
	struct gfsc_node *node);
int set_mountgroups_group(int *count, struct gfsc_mountgroup **mgs_out);
int set_mountgroup_nodes_group(struct mountgroup *mg, int option,
	int *node_count, struct gfsc_node **nodes_out);
int set_group_mode(void);

/* main.c */
int do_read(int fd, void *buf, size_t count);
int do_write(int fd, void *buf, size_t count);
void client_dead(int ci);
int client_add(int fd, void (*workfn)(int ci), void (*deadfn)(int ci));
int client_fd(int ci);
void client_ignore(int ci, int fd);
void client_back(int ci, int fd);
struct mountgroup *create_mg(char *name);
struct mountgroup *find_mg(char *name);
struct mountgroup *find_mg_id(uint32_t id);
void client_reply_remount(struct mountgroup *mg, int ci, int result);
void client_reply_join(int ci, struct gfsc_mount_args *ma, int result);
void client_reply_join_full(struct mountgroup *mg, int result);
void query_lock(void);
void query_unlock(void);
void process_connection(int ci);
void cluster_dead(int ci);

/* member_cman.c */
int setup_cman(void);
void close_cman(void);
void process_cman(int ci);
void kick_node_from_cluster(int nodeid);

/* plock.c */
int setup_plocks(void);
void process_plocks(int ci);
int limit_plocks(void);
void receive_plock(struct mountgroup *mg, char *buf, int len, int from);
void receive_own(struct mountgroup *mg, char *buf, int len, int from);
void receive_sync(struct mountgroup *mg, char *buf, int len, int from);
void receive_drop(struct mountgroup *mg, char *buf, int len, int from);
void process_saved_plocks(struct mountgroup *mg);
int unlink_checkpoint(struct mountgroup *mg);
void store_plocks(struct mountgroup *mg, int nodeid);
void retrieve_plocks(struct mountgroup *mg);
void purge_plocks(struct mountgroup *mg, int nodeid, int unmount);
int fill_plock_dump_buf(struct mountgroup *mg);
int setup_misc_devices(void);

/* util.c */
int we_are_in_fence_domain(void);
int set_sysfs(struct mountgroup *mg, char *field, int val);
int read_sysfs_int(struct mountgroup *mg, char *field, int *val_out);
int run_dmsetup_suspend(struct mountgroup *mg, char *dev);
void update_dmsetup_wait(void);
void update_flow_control_status(void);
int check_uncontrolled_filesystems(void);

/* logging.c */

void init_logging(void);
void setup_logging();
void close_logging(void);

#endif
