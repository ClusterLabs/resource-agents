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
#include <syslog.h>

int log_is_open = 0;
int log_is_verbose = 0;

void log_set_verbose(void){
  log_is_verbose = 1;
}

void log_open(const char *ident, int option, int facility){
  openlog(ident, option, facility);
  log_is_open = 1;
}

void log_close(void){
  log_is_open = 0;
}
