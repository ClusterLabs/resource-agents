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

#include "libcman.h"

extern cman_handle_t ch;
extern int cman_cb;
extern int cman_reason;

int can_shutdown(void *private);
int default_process_member(void);
int setup_member(void *private);
void exit_member(void);
