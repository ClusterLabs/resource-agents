#ifndef _DEPENDS_H
#define _DEPENDS_H

#include <resgroup.h>
#include <list.h>

typedef enum {
	DEP_COLO_UNSPEC		= 0,
	DEP_COLO_ALWAYS		= 1,
	DEP_COLO_NEVER		= 2
} dep_colo_t;

typedef enum {
	DEP_REQ_UNSPEC		= 0,
	DEP_REQ_START		= 1,
	DEP_REQ_ALWAYS		= 2
} dep_req_t;

typedef enum {
	DEP_FLAG_CYCLIC		= 0x1,
	DEP_FLAG_TERMINAL	= 0x2,
	DEP_FLAG_IMPOSSIBLE	= 0x4,
	DEP_FLAG_IMPLIED	= 0x8,
	DEP_FLAG_NEVER		= 0x10,
	DEP_FLAG_ALWAYS		= 0x20
} dep_flag_t;

typedef enum {
	RS_ILLEGAL_NODE	= 0x1,
	RS_DEAD_NODE	= 0x2,
	RS_BEEN_STARTED	= 0x4,
	RS_BEEN_STOPPED = 0x8,
	RS_IMMUTABLE    = 0x10,
	RS_ORDERED	= 0x20,
	RS_FAILBACK	= 0x40,
	RS_BROKEN	= 0x80
} rs_flag_t;

typedef enum {
	DN_BROKEN_COLO	= 0x1,
	DN_BROKEN_NONCOLO	= 0x2,
	DN_BROKEN_REQ	= 0x4
} dep_node_flag_t;

typedef struct _dn_node {
	list_head();
	char		*dn_name;
	struct _dep	*dn_ptr;
	dep_colo_t	dn_colo;
	dep_req_t	dn_req;
	int		dn_traversed;
	dep_node_flag_t	dn_flags;
} dep_node_t;

typedef struct _dep {
	list_head();
	char *d_name;
	dep_node_t *d_nodes;
	dep_flag_t d_flags;
	int d_hits;
	int d_deps;
} dep_t;


typedef struct _res_state {
	rg_state_t	rs_status;
	int 		*rs_allowed;
	int		rs_allowed_len;
	rs_flag_t	rs_flags;
} dep_rs_t;


/* List of operations to take current state -> ideal state */
typedef struct _dep_op {
	list_head();
	struct _dep_op	*do_child;
	char		do_res[64];	
	int		do_op;
	int		do_nodeid;
	int		do_iter;
} dep_op_t;
	


int construct_depends(int ccsfd, dep_t **deps);
void deconstruct_depends(dep_t **deps);
void print_depends(FILE *fp, dep_t **deps);
void print_depends_dot(FILE *fp, dep_t **deps);

/* Check cluster state given:
 * all resource (service) states,
 * all available nodes to each resource,
 * all online nodes.
 * 
 * Returns # of errors (negative), # of stopped services (positive), or
 * 0 if the cluster state is ideal.
 * 
 * Note: Call dep_reset() when you're done to clear error flags in the
 * graph.
 */
int dep_check(dep_t **deps, dep_rs_t *states, int slen,
		    int *nodes, int nlen);

/* Clear error flags in the graph + states */
void dep_reset(dep_t **deps, dep_rs_t *rs, int slen);

/* Print out our errors */
void dep_print_errors(dep_t **deps, dep_rs_t *rs, int slen);

dep_rs_t * dep_rstate_alloc(resource_node_t **restree, fod_t **domains,
			     int *nodes, int nlen, int *rs_cnt);
void dep_rstate_free(dep_rs_t *states, int cnt);

/* Dump graphviz-compatible output to fp (includes errors in graph */
int dep_cluster_state_dot(FILE *fp, dep_t **deps, dep_rs_t *states, int slen,
			  int *nodes, int nlen);
int dep_cluster_state(FILE *fp, dep_t **deps, dep_rs_t *states, int slen,
		      int *nodes, int nlen);

/* Calculate transition list */
int dep_calc_trans(dep_t **deps, dep_rs_t *states, int slen,
		   int *nodes, int nlen, dep_op_t **op_list, int *iter);

int dep_copy_tree(dep_t **dest, dep_t **src);

int dep_apply_trans(dep_t **deps, dep_rs_t *states,
		    int slen, dep_op_t **op_list);

int dep_check_operation(char *res, int operation, int target, 
		    dep_t **deps, dep_rs_t *_states,
		    int slen, int *nodes, int nlen, dep_op_t **oplist);
int dep_tree_dup(dep_t **dest, dep_t **src);

#endif
