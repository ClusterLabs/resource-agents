/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __procinfo_h__
#define __procinfo_h__
int add_to_proc (gulm_fs_t * fs);
void remove_from_proc (gulm_fs_t * fs);
void remove_locktables_from_proc (void);
void add_locktables_to_proc (void);
int init_proc_dir (void);
void remove_proc_dir (void);
#endif /*__procinfo_h__*/
