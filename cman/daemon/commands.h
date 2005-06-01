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

extern int process_cnxman_message(char *data,
				  int len, char *addr, int addrlen,
				  struct cluster_node *rem_node);

extern int send_to_user_port(struct cl_protheader *header,
			     struct msghdr *msg,
			     char *recv_buf, int len);
extern void clean_dead_listeners(void);
extern void unbind_con(struct connection *con);
extern void commands_init(void);
extern int process_command(struct connection *con, int cmd, char *cmdbuf,
			   char **retbuf, int *retlen, int retsize, int offset);

extern int config_version;
