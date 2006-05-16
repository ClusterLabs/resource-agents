/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2006 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "libgroup.h"

enum {
  DO_STOP = 1,
  DO_START,
  DO_FINISH,
  DO_TERMINATE,
  DO_SETID,
};

extern group_handle_t gh;
extern int cb_action;
extern char cb_name[MAX_GROUP_NAME_LEN+1];
extern int cb_event_nr;
extern int cb_id;
extern int cb_type;
extern int cb_member_count;
extern int cb_members[MAX_GROUP_MEMBERS];

int default_process_groupd(void);
int setup_groupd(char *name);
void exit_groupd(void);
