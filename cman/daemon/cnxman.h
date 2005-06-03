/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __CNXMAN_H
#define __CNXMAN_H

extern int cl_sendmsg(struct connection *con,
		      int port, int nodeid, int flags,
		      char *msg, size_t size);
extern int comms_receive_message(struct cl_comms_socket *csock);
extern struct cl_comms_socket *add_clsock(int broadcast, int number, int fd);
extern int cluster_init(void);
extern void check_mainloop_flags();


extern void set_quorate(int);
extern int current_interface_num(void);
extern void get_local_addresses(struct cluster_node *node);
extern int add_node_address(struct cluster_node *node, unsigned char *addr, int len);
extern int new_temp_nodeid(char *addr, int addrlen);
extern int get_addr_from_temp_nodeid(int nodeid, char *addr, unsigned int *addrlen);
extern void purge_temp_nodeids(void);
extern int send_or_queue_message(void *buf, int len,
			  struct sockaddr_cl *caddr,
			  unsigned int flags);
extern int queue_message(struct connection *con, void *buf, int len,
			 struct sockaddr_cl *caddr,
			 unsigned char port, int flags);
extern char *get_interface_addresses(char *ptr);

extern struct cluster_node *quorum_device;
extern uint16_t cluster_id;
extern char cluster_name[MAX_CLUSTER_NAME_LEN+1];
extern unsigned short two_node;
extern int use_count;
extern unsigned int address_length;
extern int cnxman_running;
extern int num_interfaces;


#define min(a,b) (((a) < (b)) ? (a) : (b))
#define max(a,b) (((a) > (b)) ? (a) : (b))


#endif
