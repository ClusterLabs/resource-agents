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

#ifndef __fence_h__
#define __fence_h__

int update_timestamp_list(char *node, uint64_t timestamp);
int check_banned_list(char *node);
int add_to_banned_list(char *node);
void remove_from_banned_list(char *node);
int list_banned(char **buffer, uint32_t *list_size);

#endif /* __fence_h__ */
