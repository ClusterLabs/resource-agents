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

void process_barrier_msg(struct cl_barriermsg *msg,
			 struct cluster_node *node);
int do_cmd_barrier(struct connection *con, char *cmdbuf, int *retlen);
void barrier_init(void);
void check_barrier_returns(void);
void remove_barriers(struct connection *con);
