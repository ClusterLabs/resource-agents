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

/*
 * This file opens and closes a socket.
 * In kernel and/or userspace.
 */

#include "xdr.h"

#ifdef __KERNEL__
#ifdef __linux__

int
xdr_open (xdr_socket * xsk)
{
	return sock_create (AF_INET6, SOCK_STREAM, 0, xsk);
}

int
xdr_connect (struct sockaddr_in6 *adr, xdr_socket xsk)
{
	return xsk->ops->connect (xsk,
				  (struct sockaddr *) adr,
				  sizeof (struct sockaddr_in6), 0);
}

void
xdr_close (xdr_socket * xsk)
{
	if (*xsk == NULL)
		return;
	sock_release (*xsk);
	*xsk = NULL;
}

#endif /*__linux__*/
#else /*__KERNEL__*/

int
xdr_open (xdr_socket * xsk)
{
	int sk;
	sk = socket (AF_INET6, SOCK_STREAM, 0);
	if (sk < 0)
		return -errno;
	*xsk = sk;
	return 0;
}

int
xdr_connect (struct sockaddr_in6 *adr, xdr_socket xsk)
{
	int err;
	err =
	    connect (xsk, (struct sockaddr *) adr,
		     sizeof (struct sockaddr_in6));
	if (err < 0)
		return -errno;
	return 0;
}

void
xdr_close (xdr_socket * xsk)
{
	if (*xsk < 0)
		return;
	close (*xsk);
	*xsk = -1;
}

#endif /*__KERNEL__*/
