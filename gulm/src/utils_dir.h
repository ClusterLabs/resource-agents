/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  2002-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/
#ifndef __utils_dir_h__
#define __utils_dir_h__
int open_tmp_file(char *file);
void pid_lock(char *path, char *lf);
void clear_pid(char *path, char *lf);
#endif /*__utils_dir_h__*/
/* vim: set ai cin et sw=3 ts=3 : */
