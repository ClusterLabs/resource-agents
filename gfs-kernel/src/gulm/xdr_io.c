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
 * does the lowest level of reads and writes.
 * In kernel and/or userspace.
 */

#include "xdr.h"

#ifdef __KERNEL__
#ifdef __linux__
#include <linux/net.h>
#include <linux/in.h>
#include <linux/socket.h>
#include <net/sock.h>
#include "asm/uaccess.h"

/**
 * do_tfer - transfers data over a socket
 * @sock: < socket
 * @iov: <> iovec of buffers
 * @n:    < how many iovecs
 * @size: < total data size to send/recv
 * @dir:  < send or recv
 * @timeout: < how many sec to wait. 0 == forever.
 * 
 * Returns: <0: Error
 *         >=0: Bytes transfered
 */
static int
do_tfer (struct socket *sock, struct iovec *iov, int n, int size, int dir)
{
	unsigned long flags;
	sigset_t oldset;
	struct msghdr m;
	mm_segment_t fs;
	int rv, moved = 0;

	fs = get_fs ();
	set_fs (get_ds ());

	/* XXX do I still want the signal stuff? */
	spin_lock_irqsave (&current->sighand->siglock, flags);
	oldset = current->blocked;
	siginitsetinv (&current->blocked,
		       sigmask (SIGKILL) | sigmask (SIGTERM));
	recalc_sigpending ();
	spin_unlock_irqrestore (&current->sighand->siglock, flags);

	memset (&m, 0, sizeof (struct msghdr));
	for (;;) {
		m.msg_iov = iov;
		m.msg_iovlen = n;
		m.msg_flags = MSG_NOSIGNAL;

		if (dir)
			rv = sock_sendmsg (sock, &m, size - moved);
		else
			rv = sock_recvmsg (sock, &m, size - moved, 0);

		if (rv <= 0)
			goto out_err;
		moved += rv;

		if (moved >= size)
			break;

		/* adjust iov's for next transfer */
		while (iov->iov_len == 0) {
			iov++;
			n--;
		}

	}
	rv = moved;
      out_err:
	spin_lock_irqsave (&current->sighand->siglock, flags);
	current->blocked = oldset;
	recalc_sigpending ();
	spin_unlock_irqrestore (&current->sighand->siglock, flags);

	set_fs (fs);

	return rv;
}

size_t
xdr_send (struct socket * sock, void *buf, size_t size)
{
	struct iovec iov;
	int res;

	iov.iov_base = buf;
	iov.iov_len = size;

	res = do_tfer (sock, &iov, 1, size, 1);

	return res;
}

size_t
xdr_recv (struct socket * sock, void *buf, size_t size)
{
	struct iovec iov;
	int res;

	iov.iov_base = buf;
	iov.iov_len = size;

	res = do_tfer (sock, &iov, 1, size, 0);

	return res;
}

#endif /*__linux__*/
#else /*__KERNEL__*/

#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>

ssize_t
xdr_recv (int fd, void *buf, size_t len)
{
	ssize_t cnt = 0;
	size_t ttl = 0;
	while (len > 0) {
		cnt = recv (fd, buf, len, 0);
		if (cnt == 0)
			return 0;
		if (cnt < 0)
			return -errno;
		len -= cnt;
		buf += cnt;
		ttl += cnt;
	}
	return ttl;
}

ssize_t
xdr_send (int fd, void *buf, size_t len)
{
	ssize_t cnt = 0;
	size_t ttl = 0;
	while (len > 0) {
		cnt = send (fd, buf, len, 0);
		if (cnt == 0)
			return 0;
		if (cnt < 0)
			return -errno;
		len -= cnt;
		buf += cnt;
		ttl += cnt;
	}
	return ttl;
}

#endif /*__KERNEL__*/
