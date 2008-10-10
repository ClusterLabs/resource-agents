#ifndef _LIBDLMCONTROL_H_
#define _LIBDLMCONTROL_H_

#define DLMC_DUMP_SIZE		(1024 * 1024)

#define DLMC_NF_MEMBER		0x00000001 /* node is member in cg */
#define DLMC_NF_START		0x00000002 /* start message recvd for cg */
#define DLMC_NF_DISALLOWED	0x00000004 /* node disallowed in cg */
#define DLMC_NF_CHECK_FENCING	0x00000008
#define DLMC_NF_CHECK_QUORUM	0x00000010
#define DLMC_NF_CHECK_FS	0x00000020

struct dlmc_node {
	int nodeid;
	uint32_t flags;
	uint32_t added_seq;
	uint32_t removed_seq;
	int failed_reason;
};

struct dlmc_change {
	int member_count;
	int joined_count;
	int remove_count;
	int failed_count;
	int wait_condition;	/* 0 no, 1 fencing, 2 quorum, 3 fs */
	int wait_messages;	/* 0 no, 1 yes */
	uint32_t seq;
	uint32_t combined_seq;
};

#define DLMC_LF_JOINING		0x00000001
#define DLMC_LF_LEAVING		0x00000002
#define DLMC_LF_KERNEL_STOPPED	0x00000004
#define DLMC_LF_FS_REGISTERED	0x00000008
#define DLMC_LF_NEED_PLOCKS	0x00000010
#define DLMC_LF_SAVE_PLOCKS	0x00000020

struct dlmc_lockspace {
	int group_mode;
	struct dlmc_change cg_prev;	/* completed change (started_change) */
	struct dlmc_change cg_next;	/* in-progress change (changes list) */
	uint32_t flags;
	uint32_t global_id;
	char name[DLM_LOCKSPACE_LEN+1];
};

/* dlmc_lockspace_nodes() types

   MEMBERS: members in completed (prev) change,
            zero if there's no completed (prev) change
   NEXT:    members in in-progress (next) change,
            zero if there's no in-progress (next) change
   ALL:     NEXT + nonmembers if there's an in-progress (next) change,
            MEMBERS + nonmembers if there's no in-progress (next) change, but
            there is a completed (prev) change
            nonmembers if there's no in-progress (next) or completed (prev)
            change (possible?)

   dlmc_node_info() returns info for in-progress (next) change, if one exists,
   otherwise it returns info for completed (prev) change.
*/

#define DLMC_NODES_ALL		1
#define DLMC_NODES_MEMBERS	2
#define DLMC_NODES_NEXT		3

int dlmc_dump_debug(char *buf);
int dlmc_dump_plocks(char *name, char *buf);
int dlmc_lockspace_info(char *lsname, struct dlmc_lockspace *ls);
int dlmc_node_info(char *lsname, int nodeid, struct dlmc_node *node);
int dlmc_lockspaces(int max, int *count, struct dlmc_lockspace *lss);
int dlmc_lockspace_nodes(char *lsname, int type, int max, int *count,
			 struct dlmc_node *nodes);

#define DLMC_RESULT_REGISTER	1
#define DLMC_RESULT_NOTIFIED	2

int dlmc_fs_connect(void);
void dlmc_fs_disconnect(int fd);
int dlmc_fs_register(int fd, char *name);
int dlmc_fs_unregister(int fd, char *name);
int dlmc_fs_notified(int fd, char *name, int nodeid);
int dlmc_fs_result(int fd, char *name, int *type, int *nodeid, int *result);

int dlmc_deadlock_check(char *name);

#endif

