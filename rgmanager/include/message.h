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
	M_TRY_SHUTDOWN = 8,		/* Local node shutdown */
	M_CONFIG_UPDATE = 9		/* (new) Config Update */
} msg_control_t;

typedef enum {
	MSG_NONE = -1,
	MSG_SOCKET  = 1,
	MSG_CLUSTER = 2
} msgctx_type_t;

/* Header is never presented to applications */
typedef struct ALIGNED {
	uint32_t	src_ctx;
	uint32_t	src_nodeid;
	/* 8 */
	uint32_t	dest_ctx;
	uint32_t	dest_nodeid;
	/* 16 */
	uint8_t		msg_control;
	uint8_t		msg_port;
	uint8_t		pad[2];
	/* 20 */
	uint8_t		msg_reserved[12];
} cluster_msg_hdr_t;

/* Header is never presented to applications */
typedef struct ALIGNED {
	uint32_t	msg_len;	/* Size of tailing message */
	uint8_t		msg_control;
	uint8_t		pad[3];
} local_msg_hdr_t;
/* No need for swabbing this one */


/* Cluster private queue */
typedef struct ALIGNED {
	list_head();
	cluster_msg_hdr_t *message;
	int len;
} msg_q_t;


#define swab_cluster_msg_hdr_t(ptr) \
{\
	swab32((ptr)->dest_ctx);\
	swab32((ptr)->src_ctx);\
	swab32((ptr)->src_nodeid);\
	swab32((ptr)->dest_nodeid);\
}


typedef struct ALIGNED _msgctx {
	struct _msg_ops *ops;
	msgctx_type_t type;
	int flags;
	/* XXX todo make this opaque */
	void *sp;
	union {
		struct {
			msg_q_t *queue;
			pthread_mutex_t mutex;
			pthread_cond_t cond;
			cman_handle_t cman_handle;
			int nodeid;
			int port;
			uint32_t local_ctx;
			uint32_t remote_ctx;
			int select_pipe[2];
		} cluster_info;
		struct {
			int sockfd;
			int pad;
		} local_info;
	} u;
} msgctx_t;


typedef int (*msg_open_t)(int type, int nodeid, int port, msgctx_t *ctx,
			  int timeout);
typedef int (*msg_close_t)(msgctx_t *);
typedef int (*msg_listen_t)(int me, void *, msgctx_t **);
typedef int (*msg_accept_t)(msgctx_t *, msgctx_t *);
typedef int (*msg_shutdown_t)(void);
typedef int (*msg_send_t)(msgctx_t *, void *, size_t);
typedef int (*msg_receive_t)(msgctx_t *, void *, size_t, int);
typedef int (*msg_wait_t)(msgctx_t *, int);
typedef int (*msg_fd_set_t)(msgctx_t *, fd_set *, int *);
typedef int (*msg_fd_isset_t)(msgctx_t *, fd_set *);
typedef int (*msg_fd_clr_t)(msgctx_t *, fd_set *);
typedef void (*msg_print_t)(msgctx_t *);
typedef int (*msg_init_t)(msgctx_t *);

typedef struct _msg_ops {
	msg_open_t	mo_open;
	msg_close_t	mo_close;
	msg_listen_t	mo_listen;
	msg_accept_t	mo_accept;
	msg_shutdown_t	mo_shutdown;
	msg_wait_t	mo_wait;
	msg_send_t	mo_send;
	msg_receive_t	mo_receive;
	msg_fd_set_t	mo_fd_set;
	msg_fd_isset_t	mo_fd_isset;
	msg_fd_clr_t	mo_fd_clr;
	msg_print_t	mo_print;
	msg_init_t	mo_init;
} msg_ops_t;


/* Ripped from ccsd's setup_local_socket */
#define MAX_CONTEXTS 128  /* Testing; production should be 1024-ish */

#define SKF_LISTEN (1<<0)
#define SKF_READ   (1<<1)
#define SKF_WRITE  (1<<2)
#define SKF_MCAST  (1<<3)


/* Call once for MSG_CLUSTER, once for MSG_SOCKET */
/* Private is should be a null-terminated char string for MSG_SOCKET,
   and a pointer to int type for MSG_CLUSTER */
int msg_listen(int type, void *port, int me, msgctx_t **new_ctx);
int msg_open(int type, int nodeid, int port, msgctx_t *ctx, int timeout);
int msg_init(msgctx_t *ctx);
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
void msg_print(msgctx_t *ctx);
int msg_shutdown(void);

#endif
