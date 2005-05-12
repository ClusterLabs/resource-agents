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

#ifndef __MEMBER_SYSFS_DOT_H__
#define __MEMBER_SYSFS_DOT_H__

int dlm_member_sysfs_init(void);
void dlm_member_sysfs_exit(void);
int dlm_kobject_setup(struct dlm_ls *ls);

#endif                          /* __MEMBER_SYSFS_DOT_H__ */

