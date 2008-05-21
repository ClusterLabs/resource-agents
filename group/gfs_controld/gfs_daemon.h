/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005-2008 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

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
#include <openais/cpg.h>

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
   openais/cpg.h.  There are no max defines in gfs-kernel for
   mountgroup members. (FIXME verify gfs-kernel/lock_dlm) */

#define MAX_NODES       128

/* Max string length printed on a line, for debugging/dump output. */

#define MAXLINE         256

extern int daemon_debug_opt;
extern int daemon_quit;
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

void daemon_dump_save(void);

#define log_debug(fmt, args...) \
do { \
	snprintf(daemon_debug_buf, 255, "%ld " fmt "\n", time(NULL), ##args); \
	if (daemon_debug_opt) fprintf(stderr, "%s", daemon_debug_buf); \
	daemon_dump_save(); \
} while (0)

#define log_group(g, fmt, args...) \
do { \
	snprintf(daemon_debug_buf, 255, "%ld %s " fmt "\n", time(NULL), \
		 (g)->name, ##args); \
	if (daemon_debug_opt) fprintf(stderr, "%s", daemon_debug_buf); \
	daemon_dump_save(); \
} while (0)

#define log_plock(g, fmt, args...) \
do { \
	snprintf(daemon_debug_buf, 255, "%ld %s " fmt "\n", time(NULL), \
		 (g)->name, ##args); \
	if (cfgd_plock_debug) fprintf(stderr, "%s", daemon_debug_buf); \
} while (0)

#define log_error(fmt, args...) \
do { \
	log_debug(fmt, ##args); \
	syslog(LOG_ERR, fmt, ##args); \
} while (0)

#define ASSERT(x) \
do { \
	if (!(x)) { \
		log_error("Assertion failed on line %d of file %s\n" \
			  "Assertion:  \"%s\"\n", __LINE__, __FILE__, #x); \
	} \
} while (0)

struct mountgroup {
	struct list_head	list;
	uint32_t		id;
	struct gfsc_mount_args	mount_args;
	char			name[GFS_MOUNTGROUP_LEN+1];
	int			old_group_mode;

	int			mount_client;
	int			mount_client_fd;
	int			mount_client_result;
	int			mount_client_notified;
	int			mount_client_delay;
	int			remount_client;

	int			withdraw;
	int			dmsetup_wait;
	pid_t			dmsetup_pid;

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
	int			kernel_mount_error;
	int			kernel_mount_done;
	int			got_kernel_mount;
	int			first_mount_pending_stop;
	int			first_mounter;
	int			first_mounter_done;
	int			global_first_recover_done;
	int			emulate_first_mounter;
	int			wait_first_done;
	int			low_nodeid;
	int			master_nodeid;
	int			reject_mounts;
	int			needs_recovery;
	int			our_jid;
	int			spectator;
	int			readonly;
	int			rw;
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
void read_ccs(void);
void read_ccs_nodir(struct mountgroup *mg, char *buf);

/* cpg-old.c */
int setup_cpg_old(void);
void process_cpg_old(int ci);
int send_group_message_old(struct mountgroup *mg, int len, char *buf);
void save_message_old(struct mountgroup *mg, char *buf, int len, int from,
		      int type);
void send_withdraw_old(struct mountgroup *mg);
void ping_kernel_mount_old(char *table);
int join_mountgroup_old(int ci, struct gfsc_mount_args *ma);
int kernel_recovery_done_old(char *table);
int remount_mountgroup_old(int ci, struct gfsc_mount_args *ma);
int leave_mountgroup_old(char *table, int mnterr);
void mount_done_old(struct gfsc_mount_args *ma, int result);
int do_stop(struct mountgroup *mg);
int do_finish(struct mountgroup *mg);
void do_start(struct mountgroup *mg, int type, int member_count, int *nodeids);
int do_terminate(struct mountgroup *mg);
int do_withdraw_old(char *table);
void update_flow_control_status(void);

/* group.c */
int setup_groupd(void);
void process_groupd(int ci);

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
void client_reply_remount(struct mountgroup *mg, int result);
void client_reply_join(int ci, struct gfsc_mount_args *ma, int result);
void client_reply_join_full(struct mountgroup *mg, int result);
void query_lock(void);
void query_unlock(void);
void process_connection(int ci);

/* member_cman.c */
int setup_cman(void);
void process_cman(int ci);

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

/* util.c */
int we_are_in_fence_domain(void);
int set_sysfs(struct mountgroup *mg, char *field, int val);
int read_sysfs_int(struct mountgroup *mg, char *field, int *val_out);
int run_dmsetup_suspend(struct mountgroup *mg, char *dev);
void update_dmsetup_wait(void);

#endif
