#ifndef _LIBFENCED_H_
#define _LIBFENCED_H_

#define FENCED_DUMP_SIZE (1024 * 1024)

/* for querying local node info */
#define FENCED_NODEID_US 0

struct fenced_node {
	int nodeid;
	int member;
	int victim;
	int last_fenced_master;
	int last_fenced_how;
	uint64_t last_fenced_time;
};

struct fenced_domain {
	int group_mode;
	int member_count;
	int victim_count;
	int master_nodeid;
	int current_victim;
	int state;
};

/* fenced_domain_nodes() types */
#define FENCED_NODES_ALL	1
#define FENCED_NODES_MEMBERS	2
#define FENCED_NODES_VICTIMS	3

int fenced_join(void);
int fenced_leave(void);
int fenced_dump_debug(char *buf);
int fenced_external(char *name);
int fenced_node_info(int nodeid, struct fenced_node *node);
int fenced_domain_info(struct fenced_domain *domain);
int fenced_domain_nodes(int type, int max, int *count, struct fenced_node *nodes);

#endif
