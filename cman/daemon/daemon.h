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

extern int send_status_return(struct connection *con, uint32_t cmd, int status);
extern int send_data_reply(struct connection *con, int nodeid, int port, char *data, int len);
extern void set_cman_timeout(int secs);
extern void notify_listeners(struct connection *con, int reason, int arg);
extern void cman_set_realtime(void);
extern int cman_init(void);
extern int cman_finish(void);


extern volatile sig_atomic_t quit_threads;
extern int num_connections;
