/*
 * Copyright (C) 2005 Red Hat, Inc.
 *
 * This file is released under the LGPL.
 */

#ifndef __DM_CMIRROR_SERVER_H__
#define __DM_CMIRROR_SERVER_H__

int suspend_server(void);
int resume_server(void);
int start_server(void);
void stop_server(void);
void print_server_status(struct log_c *lc);

#endif /* __DM_CMIRROR_SERVER_H__ */
