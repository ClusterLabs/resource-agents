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

struct monitor_info_s {
  int minor_nr;
  int timeout;
};
typedef struct monitor_info_s monitor_info_t;

int do_add_monitored_dev(int minor_nr, int timeout);
int do_remove_monitored_dev(int minor_nr);
int do_list_monitored_devs(monitor_info_t **devs, int *count);

#endif /* __gnbd_monitor_h__ */
