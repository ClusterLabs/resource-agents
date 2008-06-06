#ifndef __GFS_CONTROLD_DOT_H__
#define __GFS_CONTROLD_DOT_H__

/* This defines the interface between gfs_controld and libgfscontrol, and
   should only be used by libgfscontrol. */

#define GFSC_SOCK_PATH                  "gfsc_sock"
#define GFSC_QUERY_SOCK_PATH            "gfsc_query_sock"

#define GFSC_MAGIC                      0x6F5C6F5C
#define GFSC_VERSION                    0x00010001

#define GFSC_CMD_DUMP_DEBUG             1
#define GFSC_CMD_DUMP_PLOCKS            2
#define GFSC_CMD_MOUNTGROUP_INFO        3
#define GFSC_CMD_NODE_INFO              4
#define GFSC_CMD_MOUNTGROUPS            5
#define GFSC_CMD_MOUNTGROUP_NODES       6
#define GFSC_CMD_FS_JOIN		7
#define GFSC_CMD_FS_REMOUNT		8
#define GFSC_CMD_FS_MOUNT_DONE		9
#define GFSC_CMD_FS_LEAVE		10

struct gfsc_header {
	unsigned int magic;
	unsigned int version;
	unsigned int command;
	unsigned int option;
	unsigned int len;
	int data;       /* embedded command-specific data, for convenience */
	int unused1;
	int unsued2;
	char name[GFS_MOUNTGROUP_LEN]; /* no terminating null space */
};

#endif

