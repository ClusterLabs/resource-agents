/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __gnbd_monitor_h__
#define __gnbd_monitor_h__

#define MONITOR_REQ 1
#define REMOVE_REQ  2
#define LIST_REQ    3
#define MONITOR_SUCCESS_REPLY 0

#define NORMAL_STATE 0
#define TIMED_OUT_STATE 1
#define RESET_STATE 2
#define RESTARTABLE_STATE 3
#define FENCED_STATE 4
#define FAILED_STATE 5

struct monitor_info_s {
  int minor_nr;
  int timeout;
  int state;
  char server[65];
};
typedef struct monitor_info_s monitor_info_t;

int do_add_monitored_dev(int minor_nr, int timeout, char *server);
int do_remove_monitored_dev(int minor_nr);
int do_list_monitored_devs(monitor_info_t **devs, int *count);
int check_addr_info(struct addrinfo *ai1, struct addrinfo *ai2);

#endif /* __gnbd_monitor_h__ */
