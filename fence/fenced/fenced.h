#ifndef __FENCED_DOT_H__
#define __FENCED_DOT_H__

/* This defines the interface between fenced and libfenced, and should
   only be used by libfenced. */

/* should match the same in fd.h */
#define MAX_NODENAME_LEN		255

#define FENCED_SOCK_PATH		"fenced_sock"
#define FENCED_QUERY_SOCK_PATH		"fenced_query_sock"

#define FENCED_MAGIC			0x0FE11CED
#define FENCED_VERSION			0x00010001

#define FENCED_CMD_JOIN			1
#define FENCED_CMD_LEAVE		2
#define FENCED_CMD_DUMP_DEBUG		3
#define FENCED_CMD_EXTERNAL		4
#define FENCED_CMD_NODE_INFO		5
#define FENCED_CMD_DOMAIN_INFO		6
#define FENCED_CMD_DOMAIN_NODES		7

struct fenced_header {
	unsigned int magic;
	unsigned int version;
	unsigned int command;
	unsigned int option;
	unsigned int len;
	int data;	/* embedded command-specific data, for convenience */
	int unused1;
	int unsued2;
};

#endif

