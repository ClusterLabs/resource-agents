#ifndef IPC_CMANQUORUM_H_DEFINED
#define IPC_CMANQUORUM_H_DEFINED

//#include <netinet/in.h>
#include "corosync/corotypes.h"
#include "corosync/ipc_gen.h"

// ILLEGAL value!!
#define CMANQUORUM_SERVICE 15

#define CMANQUORUM_MAX_QDISK_NAME_LEN 255


enum req_cmanquorum_types {
	MESSAGE_REQ_CMANQUORUM_GETINFO = 0,
	MESSAGE_REQ_CMANQUORUM_SETEXPECTED,
	MESSAGE_REQ_CMANQUORUM_SETVOTES,
	MESSAGE_REQ_CMANQUORUM_QDISK_REGISTER,
	MESSAGE_REQ_CMANQUORUM_QDISK_UNREGISTER,
	MESSAGE_REQ_CMANQUORUM_QDISK_POLL,
	MESSAGE_REQ_CMANQUORUM_QDISK_GETINFO,
	MESSAGE_REQ_CMANQUORUM_SETDIRTY,
	MESSAGE_REQ_CMANQUORUM_KILLNODE,
	MESSAGE_REQ_CMANQUORUM_LEAVING,
	MESSAGE_REQ_CMANQUORUM_TRACKSTART,
	MESSAGE_REQ_CMANQUORUM_TRACKSTOP
};

enum res_cmanquorum_types {
	MESSAGE_RES_CMANQUORUM_STATUS = 0,
	MESSAGE_RES_CMANQUORUM_GETINFO,
	MESSAGE_RES_CMANQUORUM_QDISK_GETINFO,
	MESSAGE_RES_CMANQUORUM_TRACKSTART,
	MESSAGE_RES_CMANQUORUM_NOTIFICATION
};

struct req_lib_cmanquorum_setvotes {
        mar_req_header_t header __attribute__((aligned(8)));
	unsigned int votes;
	int nodeid;
};

struct req_lib_cmanquorum_qdisk_register {
        mar_req_header_t header __attribute__((aligned(8)));
	int votes;
	char name[CMANQUORUM_MAX_QDISK_NAME_LEN];
};

struct req_lib_cmanquorum_qdisk_poll {
        mar_req_header_t header __attribute__((aligned(8)));
	int state;
};

struct req_lib_cmanquorum_setexpected {
        mar_req_header_t header __attribute__((aligned(8)));
	int expected_votes;
};

struct req_lib_cmanquorum_trackstart {
        mar_req_header_t header __attribute__((aligned(8)));
	unsigned int track_flags;
};

struct req_lib_cmanquorum_general {
        mar_req_header_t header __attribute__((aligned(8)));
};

#define CMANQUORUM_REASON_KILL_REJECTED    1
#define CMANQUORUM_REASON_KILL_APPLICATION 2
#define CMANQUORUM_REASON_KILL_REJOIN      3

struct req_lib_cmanquorum_killnode {
        mar_req_header_t header __attribute__((aligned(8)));
	int nodeid;
	unsigned int reason;
};

struct req_lib_cmanquorum_getinfo {
        mar_req_header_t header __attribute__((aligned(8)));
	int nodeid;
};

struct res_lib_cmanquorum_status {
        mar_res_header_t header __attribute__((aligned(8)));
};

#define CMANQUORUM_INFO_FLAG_DIRTY      1
#define CMANQUORUM_INFO_FLAG_DISALLOWED 2
#define CMANQUORUM_INFO_FLAG_TWONODE    4
#define CMANQUORUM_INFO_FLAG_QUORATE    8

struct res_lib_cmanquorum_getinfo {
        mar_res_header_t header __attribute__((aligned(8)));
	int nodeid;
	unsigned int votes;
	unsigned int expected_votes;
	unsigned int highest_expected;
	unsigned int total_votes;
	unsigned int quorum;
	unsigned int flags;
};

struct res_lib_cmanquorum_qdisk_getinfo {
        mar_res_header_t header __attribute__((aligned(8)));
	int votes;
	int state;
	char name[CMANQUORUM_MAX_QDISK_NAME_LEN];
};

struct cmanquorum_node {
	mar_uint32_t nodeid;
	mar_uint32_t state;
};

struct res_lib_cmanquorum_notification {
	mar_res_header_t header __attribute__((aligned(8)));
	mar_uint32_t quorate __attribute__((aligned(8)));
	mar_uint32_t node_list_entries __attribute__((aligned(8)));
	struct cmanquorum_node node_list[] __attribute__((aligned(8)));
};

#endif
