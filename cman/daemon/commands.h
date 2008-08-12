struct cluster_node;
struct connection;
extern void process_cnxman_message(char *data, char *addr, int addrlen,
				  struct cluster_node *rem_node);

extern int send_to_userport(unsigned char fromport, unsigned char toport,
			    int nodeid, int tgtnodeid,
			    char *recv_buf, int len,
			    int endian_conv);
extern void clean_dead_listeners(void);
extern void unbind_con(struct connection *con);
extern void commands_init(void);
extern int process_command(struct connection *con, int cmd, char *cmdbuf,
			   char **retbuf, int *retlen, int retsize, int offset);
extern void send_transition_msg(int last_memb_count, int first_trans);

extern void add_ais_node(int nodeid, uint64_t incarnation, int total_members);
extern void del_ais_node(int nodeid);
extern void add_ccs_node(char *name, int nodeid, int votes, int expected_votes);
extern void override_expected(int expected);
extern void cman_send_confchg(unsigned int *member_list, int member_list_entries,
			      unsigned int *left_list, int left_list_entries,
			      unsigned int *joined_list, int joined_list_entries);


extern void clear_reread_flags(void);
extern void remove_unread_nodes(void);

/* Startup stuff called from cmanccs: */
extern int cman_set_nodename(char *name);
extern int cman_set_nodeid(int nodeid);
extern int cman_join_cluster(struct corosync_api_v1 *api,
			     char *name, unsigned short cluster_id, int two_node,
			     int votes, int expected_votes);

extern int cluster_members;
extern uint32_t max_outstanding_messages;
