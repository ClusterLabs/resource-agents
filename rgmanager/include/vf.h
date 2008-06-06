/** @file
 * Header for vf.c.
 */
#ifndef __VF_H
#define __VF_H

#include <stdint.h>
#include <sys/types.h>
#include <msgsimple.h>

/*
 * We use this to initiate the VF protocol.  This doesn't really belong here.
 */
typedef struct __attribute__ ((packed)) _vf_msg_info {
	uint32_t	vf_command;
	uint32_t	vf_transaction;
	char 		vf_keyid[64];
	uint32_t	vf_coordinator; /* Node ID of who coordinates */
	uint32_t	vf_datalen;
	uint64_t	vf_view;
	char		vf_data[0];
} vf_msg_info_t;

#define swab_vf_msg_info_t(ptr) \
{\
	swab32((ptr)->vf_command);\
	swab32((ptr)->vf_transaction);\
	swab32((ptr)->vf_coordinator);\
	swab64((ptr)->vf_view);\
	swab32((ptr)->vf_datalen);\
}


typedef struct __attribute__ ((packed)) _vf_msg {
	generic_msg_hdr	vm_hdr;
	vf_msg_info_t	vm_msg;
} vf_msg_t;

#define swab_vf_msg_t(ptr) \
{\
	swab_generic_msg_hdr(&((ptr)->vm_hdr));\
	swab_vf_msg_info_t(&((ptr)->vm_msg));\
}

 
/*
 * Exp: Callback function proto definitions.
 */
typedef int32_t (*vf_vote_cb_t)(char *, uint64_t, void *, uint32_t);
typedef int32_t (*vf_commit_cb_t)(char *, uint64_t, void *, uint32_t);

/*
 * INTERNAL VF STRUCTURES
 */
 
 /**
 * A view node.  This holds the data from a VF_JOIN_VIEW message until it
 * is committed.
 */
typedef struct _view_node {
	struct _view_node *
			vn_next;	/**< Next pointer. */
	uint32_t 	vn_transaction;	/**< Transaction ID */
	uint32_t	vn_nodeid;	/**< Node ID of coordinator. */
	struct timeval  vn_timeout;	/**< Expiration time. */
	uint64_t	vn_viewno;	/**< View Number. */
	uint32_t	vn_datalen;	/**< Length of included data. */
	uint32_t	vn_pad;		/**< pad */
	char		vn_data[0];	/**< Included data. */
} view_node_t;


/**
 * A commit node.  This holds a commit message until it is possible to
 * resolve it with its corresponding view_node_t.
 */
typedef struct _commit_node {
	struct _commit_node *
			vc_next;	/**< Next pointer. */
	uint32_t 	vc_transaction;	/**< Transaction ID */
} commit_node_t;


/**
 * A key node.  For each type of data used, a key node is created
 * and managed by the programmer.
 */
typedef struct _key_node {
	struct _key_node *kn_next;	/**< Next pointer. */
	char	 *kn_keyid;		/**< Key ID this key node refers to. */
	uint32_t kn_pid;		/**< PID. Child process running
					  View-Formation on this key. */
	uint32_t kn_datalen;		/**< Current length of data. */
	view_node_t *kn_jvlist;		/**< Buffered join-view list. */
	commit_node_t *kn_clist;	/**< Buffered commit list. */
	uint64_t kn_viewno;		/**< Current view number of data. */
	char *kn_data;			/**< Current data. */
	int kn_tsec;			/**< Default timeout (in seconds */
	int kn_pad;			/**< pad */
	vf_vote_cb_t kn_vote_cb;	/**< Voting callback function */
	vf_commit_cb_t kn_commit_cb;	/**< Commit callback function */
} key_node_t;




/*
 * VF message types.
 */
/* Main programs handle this */
#define VF_MESSAGE		0x3000

/* Subtypes */
#define VF_JOIN_VIEW		0x3001
#define VF_VOTE			0x3002
#define VF_ABORT		0x3004
#define VF_VIEW_FORMED		0x3005
#define VF_CURRENT		0x3006
#define VF_ACK			0x3007
#define VF_NACK			0x3008

#define vf_command(x)  (x&0x0000ffff)
#define vf_flags(x)    (x&0xffff0000)

#define VFMF_AFFIRM	0x00010000


#define VF_COORD_TIMEOUT	60	/* 60 seconds MAX timeout */
#define VF_COMMIT_TIMEOUT_MIN	(2 * VF_COORD_TIMEOUT)

/* Return codes for vf_handle_msg... */
#define VFR_ERROR	100
#define VFR_TIMEOUT	101
#define VFR_OK		0
#define VFR_YES		VFR_OK
#define VFR_NO		1
#define VFR_COMMIT	2
#define VFR_ABORT	3
#define VFR_NODATA	4

/*
 * Operational flags for vf_start
 */
#define VFF_RETRY		0x1
#define VFF_IGN_CONN_ERRORS	0x2
#define VFF_IGN_WRITE_ERRORS	0x4
#define VFF_IGN_READ_ERRORS	0x8
#define VFF_IGN_ALL_ERRORS	(VFF_IGN_CONN_ERRORS|VFF_IGN_WRITE_ERRORS|\
				 VFF_IGN_READ_ERRORS)


/* 
 * VF Stuff.  VF only talks to peers.
 */
int vf_init(int, uint16_t, vf_vote_cb_t, vf_commit_cb_t);
int vf_invalidate(void);
int vf_shutdown(void);

/*
 * Returns a file descriptor on which the caller can select().
 *
 * This is a pipe which is used to notify the parent process that
 * the child has exited
 */
int vf_write(cluster_member_list_t *membership, uint32_t flags,
	     char *keyid, void *data, uint32_t datalen);
int vf_read(cluster_member_list_t *membership, char *keyid,
	    uint64_t *view, void **data, uint32_t *datalen);
int vf_key_init(char *keyid, int timeout, vf_vote_cb_t vote_cb,
		vf_commit_cb_t commit_cb);
int getuptime(struct timeval *tv);
int vf_process_msg(msgctx_t *ctx, int nodeid, generic_msg_hdr *msgp, int nbytes);

#define MSGP_VFS 0x18dcf1
#define MSGP_VFC 0x0103fab

#endif
