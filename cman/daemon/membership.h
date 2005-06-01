/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

extern struct cluster_node *find_node_by_addr(char *addr, int addr_len);
extern struct cluster_node *find_node_by_nodeid(unsigned int id);
extern struct cluster_node *find_node_by_name(char *name);
extern void free_nodeid_array(void);
extern int send_reconfigure(int param, unsigned int value);
extern int calculate_quorum(int, int, unsigned int *);
extern void recalculate_quorum(int);
extern int get_quorum(void);
extern void set_votes(int, int);
extern void stop_membership_thread(void);
extern void a_node_just_died(struct cluster_node *node, int in_cman_main);
extern int in_transition(void);
extern void cman_set_realtime(void);
extern void init_debug(int debug);
extern void wake_daemon(void);
extern long gettime(void);
extern int send_kill(int nodeid, int needack);
extern int start_membership_services();
extern int next_nodeid(void);

extern struct list cluster_members_list;
extern pthread_mutex_t cluster_members_lock;
extern int cluster_members;
extern int we_are_a_cluster_member;
extern int cluster_is_quorate;
extern struct cluster_node *us;
extern char nodename[255 + 1];
extern int wanted_nodeid;
extern long gettime(void);
extern node_state_t node_state;
extern int cluster_generation;
extern struct cluster_node *master_node;
