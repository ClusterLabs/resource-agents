#ifndef _MESSAGE_H
#define _MESSAGE_H
#include <arpa/inet.h>
#include <stdint.h>
#include <pthread.h>
#include <platform.h>
#include <libcman.h>
#include <list.h>
#include <rg_types.h>
#include <sys/types.h>
#include <sys/select.h>

typedef enum {
	M_NONE = 0,
	M_OPEN = 1,
	M_OPEN_ACK = 2,
	M_CLOSE = 3,
	M_DATA = 4,
	M_STATECHANGE = 5,		/* Node transition */
	M_PORTOPENED = 6,		/* Port opened */
	M_PORTCLOSED = 7,		/* Port closed */
	M_TRY_SHUTDOWN = 8		/* Local node shutdown */
} msg_control_t;

typedef enum {
	MSG_NONE = -1,
	MSG_SOCKET  = 1,
	MSG_CLUSTER = 2
} msgctx_type_t;

/* Header is never presented to applications */
typedef struct {
	uint32_t	dest_ctx;
	uint32_t	src_ctx;
	/* 8 */
	uint32_t	src_nodeid;
	uint8_t		msg_control;
	uint8_t		msg_port;
	uint8_t		pad[2];
	/* 16 */
} cluster_msg_hdr_t;

/* Header is never presented to applications */
typedef struct {
	uint32_t	msg_len;	/* Size of tailing message */
	uint8_t		msg_control;
	uint8_t		pad[3];
} local_msg_hdr_t;
/* No need for swabbing this one */


/* Cluster private queue */
typedef struct {
	list_head();
	cluster_msg_hdr_t *message;
	int len;
} msg_q_t;


#define swab_cluster_msg_hdr_t(ptr) \
{\
	swab32((ptr)->dest_ctx);\
	swab32((ptr)->src_ctx);\
	swab32((ptr)->src_nodeid);\
}

typedef struct {
	msgctx_type_t type;
	union {
		struct {
			pthread_mutex_t mutex;
			pthread_cond_t cond;
			msg_q_t *queue;
			cman_handle_t cman_handle;
			int nodeid;
			int port;
			uint32_t local_ctx;
			uint32_t remote_ctx;
			int select_pipe[2];
		} cluster_info;
		struct {
			int sockfd;
			int flags;
		} local_info;
	} u;
} msgctx_t;


/* Ripped from ccsd's setup_local_socket */
#define RGMGR_SOCK "/var/run/cluster/rgmanager.sk"
#define MAX_CONTEXTS 32  /* Testing; production should be 1024-ish */

#define SKF_LISTEN (1<<0)

int msg_open(int nodeid, int port, msgctx_t *ctx, int timeout);
int msg_init(chandle_t *ch);

int msg_accept(msgctx_t *listenctx, msgctx_t *acceptctx);
int msg_get_nodeid(msgctx_t *ctx);
int msg_close(msgctx_t *ctx);
int msg_receive(msgctx_t *ctx, void *msg, size_t maxlen, int timeout);
int msg_wait(msgctx_t *ctx, int timeout); /* Select-ish */
int msg_send(msgctx_t *ctx, void *msg, size_t len);
msgctx_t *msg_new_ctx(void);
void msg_free_ctx(msgctx_t *old);
int msg_fd_set(msgctx_t *ctx, fd_set *fds, int *max);
int msg_fd_isset(msgctx_t *ctx, fd_set *fds);
int msg_fd_clr(msgctx_t *ctx, fd_set *fds);

#endif
