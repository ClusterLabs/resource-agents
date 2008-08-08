#ifndef _LIBGFSCONTROL_H_
#define _LIBGFSCONTROL_H_

/* Maximum mountgroup name length, should match DLM_LOCKSPACE_LEN from
   linux/dlmconstants.h.  The libcpg limit is larger at
   CPG_MAX_NAME_LENGTH 128.  Our cpg name includes a "gfs:" prefix before
   the mountgroup name. */

#define GFS_MOUNTGROUP_LEN	64

#define GFSC_DUMP_SIZE		(1024 * 1024)

#define GFSC_NF_MEMBER			0x00000001 /* node is member in cg */
#define GFSC_NF_START			0x00000002 /* start message recvd */
#define GFSC_NF_DISALLOWED		0x00000004 /* node disallowed in cg */
#define GFSC_NF_KERNEL_MOUNT_DONE	0x00000008
#define GFSC_NF_KERNEL_MOUNT_ERROR	0x00000010
#define GFSC_NF_READONLY		0x00000020
#define GFSC_NF_SPECTATOR		0x00000040
#define GFSC_NF_CHECK_DLM		0x00000080

struct gfsc_node {
	int nodeid;
	int jid;
	uint32_t flags;
	uint32_t added_seq;
	uint32_t removed_seq;
	int failed_reason;
};

struct gfsc_change {
	int member_count;
	int joined_count;
	int remove_count;
	int failed_count;
	int wait_condition;	/* 0 no, 1 fencing, 2 quorum, 3 fs */
	int wait_messages;	/* 0 no, 1 yes */
	uint32_t seq;
	uint32_t combined_seq;
};

#define GFSC_MF_JOINING			0x00000001
#define GFSC_MF_LEAVING			0x00000002
#define GFSC_MF_KERNEL_STOPPED		0x00000004
#define GFSC_MF_KERNEL_MOUNT_DONE	0x00000008
#define GFSC_MF_KERNEL_MOUNT_ERROR	0x00000010
#define GFSC_MF_FIRST_RECOVERY_NEEDED	0x00000020
#define GFSC_MF_FIRST_RECOVERY_MSG	0x00000040
#define GFSC_MF_LOCAL_RECOVERY_BUSY	0x00000080

struct gfsc_mountgroup {
	int group_mode;
	struct gfsc_change cg_prev;	/* completed change (started_change) */
	struct gfsc_change cg_next;	/* in-progress change (changes list) */
	int journals_need_recovery;	/* count of jounals need_recovery */
	uint32_t flags;
	uint32_t global_id;
	char name[GFS_MOUNTGROUP_LEN+1];
};

/* gfsc_mountgroup_nodes() types

   MEMBERS: members in completed (prev) change,
            zero if there's no completed (prev) change
   NEXT:    members in in-progress (next) change,
            zero if there's no in-progress (next) change
   ALL:     NEXT + nonmembers if there's an in-progress (next) change,
            MEMBERS + nonmembers if there's no in-progress (next) change, but
            there is a completed (prev) change
            nonmembers if there's no in-progress (next) or completed (prev)
            change (possible?)

   gfsc_node_info() returns info for in-progress (next) change, if one exists,
   otherwise it returns info for completed (prev) change.
*/

#define GFSC_NODES_ALL		1
#define GFSC_NODES_MEMBERS	2
#define GFSC_NODES_NEXT		3

int gfsc_dump_debug(char *buf);
int gfsc_dump_plocks(char *name, char *buf);
int gfsc_mountgroup_info(char *mgname, struct gfsc_mountgroup *mg);
int gfsc_node_info(char *mgname, int nodeid, struct gfsc_node *node);
int gfsc_mountgroups(int max, int *count, struct gfsc_mountgroup *mgs);
int gfsc_mountgroup_nodes(char *mgname, int type, int max, int *count,
			 struct gfsc_node *nodes);

struct gfsc_mount_args {
	char dir[PATH_MAX];
	char type[PATH_MAX];
	char proto[PATH_MAX];
	char table[PATH_MAX];
	char options[PATH_MAX];
	char dev[PATH_MAX];
	char hostdata[PATH_MAX];
};

/*
 * mount.gfs connects to gfs_controld,
 * mount.gfs tells gfs_controld to do a join or remount,
 * mount.gfs reads the result of the join or remount from gfs_controld,
 * mount.gfs tells gfs_controld the result of the mount(2),
 * mount.gfs disconnects from gfs_controld
 */

int gfsc_fs_connect(void);
int gfsc_fs_join(int fd, struct gfsc_mount_args *ma);
int gfsc_fs_remount(int fd, struct gfsc_mount_args *ma);
int gfsc_fs_result(int fd, int *result, struct gfsc_mount_args *ma);
int gfsc_fs_mount_done(int fd, struct gfsc_mount_args *ma, int result);
void gfsc_fs_disconnect(int fd);

/*
 * mount.gfs tells gfs_controld to do a leave (due to a mount failure)
 * for unmount, gfs_controld leaves due to a message from the kernel
 */

int gfsc_fs_leave(struct gfsc_mount_args *ma, int reason);

#endif

