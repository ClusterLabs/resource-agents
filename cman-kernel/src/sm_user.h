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

#ifndef __SM_USER_DOT_H__
#define __SM_USER_DOT_H__

int sm_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg);
void sm_sock_release(struct socket *sock);
void sm_sock_bind(struct socket *sock);

#endif
