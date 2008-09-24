#ifndef __RESGROUP_H
#define __RESGROUP_H

#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/select.h>
#include <gettid.h>
#include <rg_locks.h>
#include <message.h>
#include <rg_queue.h>
#include <signals.h>

/**
 * Service state as represented on disk.
 *
 * This structure represents a service description.  This data structure
 * represents the in-memory service description.  (There is a separate
 * description of the on-disk format.)
 */
typedef struct {
	char		rs_name[64];	/**< Service name */
	/* 64 */
	uint32_t	rs_id;		/**< Service ID */
	uint32_t	rs_magic;	/**< Magic ID */
	uint32_t	rs_owner;	/**< Member ID running service. */
	uint32_t	rs_last_owner;	/**< Last member to run the service. */
	/* 80 */
	uint32_t	rs_state;	/**< State of service. */
	uint32_t	rs_restarts;	/**< Number of cluster-induced 
					     restarts */
	uint64_t	rs_transition;	/**< Last service transition time */
	/* 96 */
	uint32_t	rs_flags;	/**< User setted flags */
	/* 100 */
	uint8_t		rs_version;	/**< State version */
	uint8_t		_pad_[3];
	/* 104 */
} rg_state_t;

#define swab_rg_state_t(ptr) \
{\
	swab32((ptr)->rs_id);\
	swab32((ptr)->rs_magic);\
	swab32((ptr)->rs_owner);\
	swab32((ptr)->rs_last_owner);\
	swab32((ptr)->rs_state);\
	swab32((ptr)->rs_restarts);\
	swab64((ptr)->rs_transition);\
	swab32((ptr)->rs_flags);\
}

#if 0
/* Future upgrade compatibility */
#define RG_STATE_MINSIZE		96
#define RG_STATE_CURRENT_VERSION	1

extern size_t rg_state_t_version_sizes[];
#endif


#define RG_PORT    177

/* Constants moved to src/clulib/constants.c */
/* DO NOT EDIT */
#define RG_MAGIC   0x11398fed

#define RG_ACTION_REQUEST	/* Message header */ 0x138582
/* Argument to RG_ACTION_REQUEST */
#define RG_ACTION_MASTER	0xfe0db143
#define RG_ACTION_USER		0x3f173bfd
/* */
#define RG_EVENT		0x138583

/* Requests */
#define RG_SUCCESS	  0
#define RG_FAIL		  1
#define RG_START	  2
#define RG_STOP		  3
#define RG_STATUS	  4
#define RG_DISABLE	  5
#define RG_STOP_RECOVER	  6
#define RG_START_RECOVER  7
#define RG_RESTART	  8
#define RG_EXITING	  9 
#define RG_INIT		  10
#define RG_ENABLE	  11
#define RG_STATUS_NODE	  12
#define RG_RELOCATE	  13
#define RG_CONDSTOP	  14
#define RG_CONDSTART	  15
#define RG_START_REMOTE   16	/* Part of a relocate */
#define RG_STOP_USER	  17	/* User-stop request */
#define RG_STOP_EXITING	  18	/* Exiting. */
#define RG_LOCK		  19
#define RG_UNLOCK	  20
#define RG_QUERY_LOCK	  21
#define RG_MIGRATE	  22
#define RG_FREEZE	  23
#define RG_UNFREEZE	  24
#define RG_STATUS_INQUIRY 25
#define RG_NONE		  999

const char *rg_req_str(int req);

int handle_relocate_req(char *svcName, int request, int preferred_target,
			int *new_owner);
int handle_start_req(char *svcName, int req, int *new_owner);
int handle_fd_start_req(char *svcName, int req, int *new_owner);
int handle_recover_req(char *svcName, int *new_owner);
int handle_start_remote_req(char *svcName, int req);

/* Resource group states (for now) */
#define RG_STATE_BASE			110
#define RG_STATE_STOPPED		110	/** Resource group is stopped */
#define RG_STATE_STARTING		111	/** Resource is starting */
#define RG_STATE_STARTED		112	/** Resource is started */
#define RG_STATE_STOPPING		113	/** Resource is stopping */
#define RG_STATE_FAILED			114	/** Resource has failed */
#define RG_STATE_UNINITIALIZED		115	/** Thread not running yet */
#define RG_STATE_CHECK			116	/** Checking status */
#define RG_STATE_ERROR			117	/** Recoverable error */
#define RG_STATE_RECOVER		118	/** Pending recovery */
#define RG_STATE_DISABLED		119	/** Resource not allowd to run */
#define RG_STATE_MIGRATE		120	/** Resource migrating */

#define DEFAULT_CHECK_INTERVAL		10

/* Resource group flags (for now) */
#define RG_FLAG_FROZEN			(1<<0)	/** Resource frozen */

const char *rg_state_str(int val);
const char *rg_flag_str(int val);
const char *rg_flags_str(char *flags_string, size_t size, int val, char *separator);
int rg_state_str_to_id(const char *val);
const char *rg_flags_str(char *flags_string, size_t size, int val, char *separator);
const char *agent_op_str(int val);

int eval_groups(int local, uint32_t nodeid, int nodeStatus);
int group_migrate(char *groupname, int target);

int rg_status(const char *resgroupname);
int group_op(char *rgname, int op);
void rg_init(void);

/* Basic service operations */
int svc_start(char *svcName, int req);
int svc_stop(char *svcName, int error);
int svc_status(char *svcName);
int svc_status_inquiry(char *svcName);
int svc_disable(char *svcName);
int svc_fail(char *svcName);
int svc_freeze(char *svcName);
int svc_unfreeze(char *svcName);
int svc_migrate(char *svcName, int target);
int check_restart(char *svcName);

int rt_enqueue_request(const char *resgroupname, int request,
		       msgctx_t *resp_ctx,
       		       int max, uint32_t target, int arg0, int arg1);

void send_response(int ret, int node, request_t *req);
void send_ret(msgctx_t *ctx, char *name, int ret, int orig_request,
	      int new_owner);

/* do this op on all resource groups.  The handler for the request 
   will sort out whether or not it's a valid request given the state */
void rg_doall(int request, int block, char *debugfmt);
void do_status_checks(void); /* Queue status checks for locally running
				services */

/* from rg_state.c */
int set_rg_state(char *name, rg_state_t *svcblk);
int get_rg_state(char *servicename, rg_state_t *svcblk);
int get_rg_state_local(char *servicename, rg_state_t *svcblk);
uint32_t best_target_node(cluster_member_list_t *allowed, uint32_t owner,
			  char *rg_name, int lock);

#ifdef DEBUG
int _rg_lock_dbg(char *, struct dlm_lksb *, char *, int);
#define rg_lock(name, p) _rg_lock_dbg(name, p, __FILE__, __LINE__)

int _rg_unlock_dbg(struct dlm_lksb *, char *, int);
#define rg_unlock(p) _rg_unlock_dbg(p, __FILE__, __LINE__)

#else
int rg_lock(char *name, struct dlm_lksb *p);
int rg_unlock(struct dlm_lksb *p);
#endif


/* Return codes */
#define RG_EEXCL	-16		/* Service not runnable due to
					   inability to start exclusively */
#define RG_EDOMAIN	-15		/* Service not runnable given the
					   set of nodes and its failover
					   domain */
#define RG_ESCRIPT	-14		/* S/Lang script failed */
#define RG_EFENCE	-13		/* Fencing operation pending */
#define RG_ENODE	-12		/* Node is dead/nonexistent */
#define RG_EFROZEN	-11		/* Service is frozen */
#define RG_ERUN		-10		/* Service is already running */
#define RG_EQUORUM	-9		/* Operation requires quorum */
#define RG_EINVAL	-8		/* Invalid operation for resource */
#define RG_EDEPEND 	-7		/* Operation violates dependency */
#define RG_EAGAIN	-6		/* Try again */
#define RG_EDEADLCK	-5		/* Aborted - would deadlock */
#define RG_ENOSERVICE	-4		/* Service does not exist */
#define RG_EFORWARD	-3		/* Service not mastered locally */
#define RG_EABORT	-2		/* Abort; service unrecoverable */
#define RG_EFAIL	-1		/* Generic failure */
#define RG_ESUCCESS	0
#define RG_YES		1
#define RG_NO		2


const char *rg_strerror(int val);


/*
 * Fail-over domain states
 */
#define FOD_ILLEGAL		0
#define FOD_GOOD		1
#define FOD_BETTER		2
#define FOD_BEST		3

/* 
   Fail-over domain flags
 */
#define FOD_ORDERED		(1<<0)
#define FOD_RESTRICTED		(1<<1)
#define FOD_NOFAILBACK		(1<<2)

/*
   Status tree flags
 */
#define SFL_FAILURE		(1<<0)
#define SFL_RECOVERABLE		(1<<1)

//#define DEBUG
#ifdef DEBUG

#define dbg_printf(fmt, args...) \
{\
	printf("{%d} ", gettid());\
	printf(fmt, ##args);\
	fflush(stdout);\
}

#if 0
#define pthread_mutex_lock(mutex) \
{\
	printf("lock(%s) @ %s:%d\n", #mutex, __FUNCTION__, __LINE__);\
	fflush(stdout);\
	pthread_mutex_lock(mutex);\
}


#define pthread_mutex_unlock(mutex) \
{\
	printf("unlock(%s) @ %s:%d\n", #mutex, __FUNCTION__, __LINE__);\
	fflush(stdout);\
	pthread_mutex_unlock(mutex);\
}
#endif

#else /* DEBUG */

#define dbg_printf(fmt, args...)

#endif

#endif
