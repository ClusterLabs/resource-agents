void process_barrier_msg(struct cl_barriermsg *msg,
			 struct cluster_node *node);
int do_cmd_barrier(struct connection *con, char *cmdbuf, int *retlen);
void barrier_init(void);
void check_barrier_returns(void);
void remove_barriers(struct connection *con);
