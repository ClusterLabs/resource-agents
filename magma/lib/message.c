/*
  Copyright Red Hat, Inc. 2002-2004
  Copyright Mission Critical Linux, 2000
                                                                                
  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2, or (at your option) any
  later version.
                                                                                
  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.
                                                                                
  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to the
  Free Software Foundation, Inc.,  675 Mass Ave, Cambridge,
  MA 02139, USA.
*/
/** @file
 * Intra-Cluster Messaging Interface for Magma.
 *
 *  authors: Jeff Moyer <jmoyer at redhat.com>
 *	     - Original msg.c from Kimberlite
 *           Lon H. Hohberger <lhh at redhat.com>
 *           - IPv6, simplification
 */
#include <magma.h>
#include <magmamsg.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <netdb.h>
#include <endian.h>
#include <byteswap.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "clist.h"

#ifdef MDEBUG
#include <mallocdbg.h>
#endif

#define IPV6_PORT_OFFSET 1

/*
   From fdops.c
 */
int _select_retry(int fdmax, fd_set * rfds, fd_set * wfds, fd_set * xfds,
		   struct timeval *timeout);
ssize_t _read_retry(int sockfd, void *buf, int count,
		     struct timeval * timeout);
ssize_t _write_retry(int fd, void *buf, int count, struct timeval * timeout);


/*
   We store a membership list with resolved addresses internally.
   Yes, it's ugly.
 */
static pthread_mutex_t ml_mutex = PTHREAD_MUTEX_INITIALIZER;
static cluster_member_list_t *ml_membership;

/* Mutex to prevent
   thread1               thread2
      create_socket
      clist_insert
                             fill fd set
      set_purpose

   This case would cause the new fd to appear 
 */
static pthread_mutex_t fill_mutex = PTHREAD_MUTEX_INITIALIZER;


/**
  Update our internal membership list with the provided list.
  Does NOT copy over resolved addresses; the caller may want to 
  reuse them for some reason on their list.

  @param membership	New membership list
  @return		0 on success, -1 on malloc failure
 */
int
msg_update(cluster_member_list_t *membership)
{
	pthread_mutex_lock(&ml_mutex);

	if (ml_membership)
		cml_free(ml_membership);

	if (membership) {
		ml_membership = cml_dup(membership);
	} else {
		ml_membership = NULL;
	}

	pthread_mutex_unlock(&ml_mutex);
	return 0;
}


/**
  Receive a message from a file descriptor.  Related of _msg_receive
  from Kimberlite.

  @param fd		File descriptor to read from.
  @param buf		Pre-allocated buffer to fill.
  @param count		Size of message expected.
  @param tv		Timeout value.
  @return 		Size of read data, or -1 on failure
 */
static ssize_t
_msg_receive(int fd, void *buf, ssize_t count,
	      struct timeval *tv)
{
	if (fd < 0) {
		errno = EBADF;
		return -1;
	}

	if (!(clist_get_flags(fd) & MSG_READ)) {
		errno = EPERM;
		return -1;
	}

	return _read_retry(fd, buf, count, tv);
}


/**
  Receive a message from a file descriptor w/o a timeout value.

  @param fd		File descriptor to receive from
  @param buf		Pre-allocated bufffer 
  @param count		Size of expected message; must be <= size of
  			preallocated buffer.
  @return		-1 on failure or size of read data
  @see			_msg_receive, msg_receive_timeout
 */
int
msg_receive(int fd, void *buf, ssize_t count)
{
	return (_msg_receive(fd, buf, count, NULL));
}


/**
  Receive a message from a file descriptor with a timeout value.

  @param fd		File descriptor to receive from
  @param buf		Pre-allocated bufffer \
  @param count		Size of expected message; must be <= size of
  			preallocated buffer.
  @param timeout	Timeout, in seconds, to wait for data.
  @return		-1 on failure or size of read data
  @see			_msg_receive, msg_receive
 */
ssize_t
msg_receive_timeout(int fd, void *buf, ssize_t count,
		    unsigned int timeout)
{
	struct timeval tv;

	tv.tv_sec = timeout;
	tv.tv_usec = 0;

	return (_msg_receive(fd, buf, count, &tv));
}


/**
  Send a message to a file descriptor.

  @param fd		File descriptor to send to
  @param buf		Buffer to send.
  @param count		Size of buffer to send.
  @return		Amount of data sent or -1 on error
 */
ssize_t
msg_send(int fd, void *buf, ssize_t count)
{
	if (fd == -1) {
		errno = EBADF;
		return -1;
	}

	if (!(clist_get_flags(fd) & MSG_WRITE)) {
		errno = EPERM;
		return -1;
	}

	return write(fd, buf, count);
}


/**
  Connect in a non-blocking fashion to the designated address.

  @param fd		File descriptor to connect
  @param dest		sockaddr (ipv4 or ipv6) to connect to.
  @param len		Length of dest
  @param timeout	Timeout, in seconds, to wait for a completed
  			connection.
  @return		0 on success, -1 on failure.
 */
static int
connect_nb(int fd, struct sockaddr *dest, socklen_t len, int timeout)
{
	int ret, flags = 1, err, l;
	fd_set rfds, wfds;
	struct timeval tv;

	/*
	 * Use TCP Keepalive
	 */
	if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &flags, sizeof(flags))<0)
		return -1;
			
	/*
	   Set up non-blocking connect
	 */
	flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);

	ret = connect(fd, dest, len);

	if ((ret < 0) && (errno != EINPROGRESS))
		return -1;

	if (ret != 0) {
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		FD_ZERO(&wfds);
		FD_SET(fd, &wfds);

		tv.tv_sec = timeout;
		tv.tv_usec = 0;
		
		if (_select_retry(fd + 1, &rfds, &wfds, NULL, &tv) == 0) {
			errno = ETIMEDOUT;
			return -1;
		}

		if (FD_ISSET(fd, &rfds) || FD_ISSET(fd, &wfds)) {
			l = sizeof(err);
			if (getsockopt(fd, SOL_SOCKET, SO_ERROR,
				       &err, &l) < 0) {
				close(fd);
				return -1;
			}

			if (err != 0) {
				close(fd);
				errno = err;
				return -1;
			}

			fcntl(fd, F_SETFL, flags);
			return 0;
		}
	}

	errno = EIO;
	return -1;
}


/**
  Connect via ipv6 socket to a given IP address, port

  @param in6_addr	IPv6 address to connect to
  @param port		Port to connect to
  @param timeout	Timeout, in seconds, to wait for a completed
  			connection
  @return 		0 on success, -1 on failure
  @see			connect_nb, ipv4_connect
 */
static int
ipv6_connect(struct in6_addr *in6_addr, uint16_t port, int timeout)
{
	struct sockaddr_in6 _sin6;
	int fd, ret;

	fd = socket(PF_INET6, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;

	memset(&_sin6, 0, sizeof(_sin6));
	_sin6.sin6_family = AF_INET6;
	_sin6.sin6_port = htons(port);
	_sin6.sin6_flowinfo = 0;
	memcpy(&_sin6.sin6_addr, in6_addr, sizeof(_sin6.sin6_addr));

	ret = connect_nb(fd, (struct sockaddr *)&_sin6, sizeof(_sin6), timeout);
	if (ret < 0) {
		close(fd);
		return -1;
	}
	return fd;
}


/**
  Connect via ipv4 socket to a given IP address, port

  @param in_addr	IPv4 address to connect to
  @param port		Port to connect to
  @param timeout	Timeout, in seconds, to wait for a completed
  			connection
  @return 		0 on success, -1 on failure
  @see			connect_nb, ipv6_connect
 */
static int
ipv4_connect(struct in_addr *in_addr, uint16_t port, int timeout)
{
	struct sockaddr_in _sin;
	int fd, ret;

	fd = socket(PF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;

	_sin.sin_family = AF_INET;
	_sin.sin_port = htons(port);
	memcpy(&_sin.sin_addr, in_addr, sizeof(_sin.sin_addr));

	ret = connect_nb(fd, (struct sockaddr *)&_sin, sizeof(_sin), timeout);
	if (ret < 0) {
		close(fd);
		return -1;
	}

	return fd;
}


/** 
  Open a TCP connection to another cluster member given the node ID and the
  base port.  

  @param nodeid		Node ID to connect to.
  @param baseport	Port to connect to.  +1 if we end up using an ipv6
  			address instead of an IPv4 one for a given member.
  @param purpose	User or application defined purpose.
  @param timeout	If specified, the connection attempt will abort
  			after this many seconds.
  @return 		File descriptor, or -1 if couldn't connect.
  @see			ipv4_connect, ipv6_connect
 */
int
msg_open(uint64_t nodeid, uint16_t baseport, int purpose, int timeout)
{
	int fd;
	cluster_member_t *nodep;
	struct addrinfo *ai;

	pthread_mutex_lock(&ml_mutex);
	nodep = memb_id_to_p(ml_membership, nodeid);
	if (!nodep) {
		pthread_mutex_unlock(&ml_mutex);
		errno = EINVAL;
		return -1;
	}

	/* Try to resolve if we haven't done so */
	if (!nodep->cm_addrs && (memb_resolve(nodep) < 0)) {
		pthread_mutex_unlock(&ml_mutex);
		errno = EFAULT;
		return -1;
	}

	/* Try IPv6 first */
	for (ai = nodep->cm_addrs; ai; ai = ai->ai_next) {
		if (ai->ai_family != AF_INET6)
			continue;

		if (ai->ai_socktype != SOCK_STREAM)
			continue;

		fd = ipv6_connect(
			&((struct sockaddr_in6 *)ai->ai_addr)->sin6_addr,
      			baseport + IPV6_PORT_OFFSET, timeout);

		if (fd >= 0) {
			pthread_mutex_unlock(&ml_mutex);

			pthread_mutex_lock(&fill_mutex);
			clist_insert(fd, MSG_OPEN | MSG_CONNECTED |
				     MSG_READ | MSG_WRITE);
			clist_set_purpose(fd, purpose);
			pthread_mutex_unlock(&fill_mutex);
			return fd;
		}
	}

	/* Try IPv4 */
	for (ai = nodep->cm_addrs; ai; ai = ai->ai_next) {
		if (ai->ai_family != AF_INET)
			continue;

		if (ai->ai_socktype != SOCK_STREAM)
			continue;

		fd = ipv4_connect(
			&((struct sockaddr_in *)ai->ai_addr)->sin_addr,
      			baseport, timeout);

		if (fd >= 0) {
			pthread_mutex_unlock(&ml_mutex);

			pthread_mutex_lock(&fill_mutex);
			clist_insert(fd, MSG_OPEN | MSG_CONNECTED |
				     MSG_READ | MSG_WRITE);
			clist_set_purpose(fd, purpose);
			pthread_mutex_unlock(&fill_mutex);
			return fd;
		}
	}

	pthread_mutex_unlock(&ml_mutex);
	errno = EHOSTUNREACH;
	return -1;
}

/**
  Set close-on-exec bit option for a socket.

  @param fd		Socket to set CLOEXEC flag
  @return		0 on success, -1 on failure
  @see			fcntl
 */
static int 
set_cloexec(int fd)
{
	int flags = fcntl(fd, F_GETFD, 0);
	flags |= FD_CLOEXEC;
	return fcntl(fd, F_SETFD, flags);
}


/**
  Bind to a port on the local IPv6 stack

  @param port		Port to bind to
  @return		0 on success, -1 on failure
  @see			ipv4_bind
 */
static int
ipv6_bind(uint16_t port)
{
	struct sockaddr_in6 _sin6;
	int fd, ret;

	fd = socket(PF_INET6, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;

	memset(&_sin6, 0, sizeof(_sin6));
	_sin6.sin6_family = AF_INET6;
	_sin6.sin6_port = htons(port);
	_sin6.sin6_flowinfo = 0;
	_sin6.sin6_addr = in6addr_any;

	ret = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &ret, sizeof (ret));

	ret = set_cloexec(fd);
	if (ret < 0) {
		close(fd);
		return -1;
	}

	ret = bind(fd, (struct sockaddr *)&_sin6, sizeof(_sin6));
	if (ret < 0) {
		close(fd);
		return -1;
	}
	return fd;
}


/**
  Bind to a port on the local IPv4 stack

  @param port		Port to bind to
  @return		0 on success, -1 on failure
  @see			ipv6_bind
 */
static int
ipv4_bind(uint16_t port)
{
	struct sockaddr_in _sin;
	int fd, ret;

	fd = socket(PF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;

	_sin.sin_family = AF_INET;
	_sin.sin_port = htons(port);
	_sin.sin_addr.s_addr = htonl(INADDR_ANY);

	ret = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &ret, sizeof (ret));
	
	ret = set_cloexec(fd);
	if (ret < 0) {
		close(fd);
		return -1;
	}

	ret = bind(fd, (struct sockaddr *)&_sin, sizeof(_sin));
	if (ret < 0) {
		close(fd);
		return -1;
	}
	return fd;
}


/**
  Set up listener sockets on a given base port.  First, we try to listen
  on IPv6.

  @param baseport	Port to listen on.  Note that this is altered by
  			IPV6_PORT_OFFSET.
  @param purpose	User or application defined purpose.
  @param ret		Preallocated array of at least two ints.  The listening
  			file descriptors are stored within; up to 2: one for
			IPv4, one for IPv6.  It should not matter to the caller
			which file descriptor corresponds to which network
			stack.
  @param retlen		Size of ret, in ints.  Must be at least 2.
  @return		Number of file descriptors listening in ret (0...2)
  @see			ipv4_bind, ipv6_bind
 */
int
msg_listen(uint16_t baseport, int purpose, int *ret, int retlen)
{
	int fd, x = 0;

	if (retlen < 2) {
		errno = EINVAL;
		return -1;
	}

	fd = ipv6_bind(baseport + IPV6_PORT_OFFSET);
	if (fd >= 0) {
		//printf("IPv6 fd = %d\n",fd);
		listen(fd, 15);
		pthread_mutex_lock(&fill_mutex);
		clist_insert(fd, MSG_OPEN | MSG_LISTEN);
		clist_set_purpose(fd, purpose);
		pthread_mutex_unlock(&fill_mutex);
		ret[x++] = fd;
	}

	fd = ipv4_bind(baseport);
	if (fd >= 0) {
		//printf("IPv4 fd = %d\n",fd);
		listen(fd, 15);
		pthread_mutex_lock(&fill_mutex);
		clist_insert(fd, MSG_OPEN | MSG_LISTEN);
		clist_set_purpose(fd, purpose);
		pthread_mutex_unlock(&fill_mutex);
		ret[x++] = fd;
	}

	return x;
}


/**
  Find a node ID by its address in network-byte-order.  Addr can be either
  a struct sockaddr_in6 or struct sockaddr_in structure.

  @param family		AF_INET or AF_INET6
  @param addr		Address to look for in ml_membership
  @return 		(uint64_t)-1 if not found or the node ID corresponding
  			to the given address.
 */
static uint64_t
find_nodeid_by_addr(int family, struct sockaddr *addr)
{
	uint64_t ret;
	int x;
	char found = 0;
	struct addrinfo *ai;

	pthread_mutex_lock(&ml_mutex);

	if (!ml_membership) {
		pthread_mutex_unlock(&ml_mutex);
		return (uint64_t)-1;
	}

	memb_resolve_list(ml_membership, NULL);

	for (x = 0; x < ml_membership->cml_count; x++) {
		if (!ml_membership->cml_members[x].cm_addrs)
			continue;

		for (ai = ml_membership->cml_members[x].cm_addrs; ai;
		     ai = ai->ai_next) {

			if (ai->ai_family != AF_INET &&
			    ai->ai_family != AF_INET6)
				continue;

			if (family == AF_INET && ai->ai_family == AF_INET) {
				if (memcmp(&((struct
					      sockaddr_in *)addr)->sin_addr,
				   	   &((struct
					      sockaddr_in *)ai->ai_addr)->sin_addr,
					   sizeof(struct in_addr)))
					continue;
				found = 1;
				break;
			}

			if (family == AF_INET6 && ai->ai_family == AF_INET6) {
				if (memcmp(&((struct
					      sockaddr_in6 *)addr)->sin6_addr,
				   	   &((struct
					      sockaddr_in6 *)ai->ai_addr)->sin6_addr,
					   sizeof(struct in6_addr)))
					continue;
				found = 1;
				break;
			}
		}

		if (found) {
			ret = ml_membership->cml_members[x].cm_id;
			pthread_mutex_unlock(&ml_mutex);
			return ret;
		}
	}
	pthread_mutex_unlock(&ml_mutex);
	return (uint64_t)-1;
}


/**
  Accept a connection from the given listening file descriptor.

  @param fd		Listening file descriptor which has a connection 
  			pending.
  @param members_only	If nonzero, reject if the connection is not from
  			a current member of ml_membership.
  @param nodeid		If non-NULL, we store the nodeid of the connecting
  			member within.
  @return		New file descriptor, or -1 in error case.
  @see			msg_listen
 */
int
msg_accept(int fd, int members_only, uint64_t *nodeid)
{
	int acceptfd;
	int p;
	uint64_t remoteid = (uint64_t)-1;
	union {
		struct sockaddr_in6 ip6addr;
		struct sockaddr_in  ip4addr;
	} cliaddr;
	struct sockaddr *cliaddrp;
	socklen_t clilen;

	if (fd < 0) {
		errno = EBADF;
		return -1;
	}

	if (!(clist_get_flags(fd) & MSG_LISTEN)) {
		errno = EPERM;
		return -1;
	}

	p = clist_get_purpose(fd);

	cliaddrp = (struct sockaddr *)&cliaddr;
	memset(cliaddrp, 0, sizeof(cliaddr));
	clilen = sizeof(cliaddr);

	do {
		acceptfd = accept(fd, cliaddrp, &clilen);

		if (acceptfd < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
	} while (0);

	remoteid = find_nodeid_by_addr(cliaddrp->sa_family, cliaddrp);
	if (members_only && (remoteid == (uint64_t)-1)) {
		close(acceptfd);
		errno = EPERM;
		return -1;
	}

	if (nodeid)
		*nodeid = remoteid;

	pthread_mutex_lock(&fill_mutex);
	clist_insert(acceptfd, MSG_OPEN | MSG_CONNECTED | MSG_READ | MSG_WRITE);
	clist_set_purpose(acceptfd, p);
	pthread_mutex_unlock(&fill_mutex);
	return acceptfd;
}


/**
  Close a file descriptor and remove it from our file descriptor list.

  @param fd		File descriptor to close
  @return		-1 on failure, 0 on success
  @see			close, msg_open
 */
int
msg_close(int fd)
{
	if (!(clist_get_flags(fd) & MSG_OPEN)) {
		errno = EPERM;
		return -1;
	}
	clist_delete(fd);
	return close(fd);
}


/**
  Fill a file descriptor set with all open file descriptors.

  @param set		Set to fill
  @param flags		Flags to look for.
  @param purpose	User or application-defined purpose to look for.
  @return 		0
 */
int
msg_fill_fdset(fd_set *set, int flags, int purpose)
{
	int rv;

	pthread_mutex_lock(&fill_mutex);
	rv = clist_fill_fdset(set, flags, purpose);
	pthread_mutex_unlock(&fill_mutex);
	return rv;
}


/**
  Return next active file descriptor in the file descriptor set.
  (The bit corresponding to the file descriptor is cleared)

  @param set		File descriptor set to check
  @return 		-1 if none available or a file descriptor
  @see msg_fill_fdset
 */
int
msg_next_fd(fd_set *set)
{
	return clist_next_set(set);
}


/**
  Set the user or application-defined purpose of a file descriptor
  (application-specific)

  @param fd		File descriptor to perform operation upon.
  @param purpose	User or application defined purpose.
  @return 		0 if successful, -1 on failure (fd not found in
  			our list.)
 */
int
msg_set_purpose(int fd, int purpose)
{
	return clist_set_purpose(fd, purpose);
}


/**
  Get the user or application-defined purpose of a file descriptor

  @param fd		File descriptor
  @return 		Purpose ID if successful, -1 on failure (fd not found
  			in our list.)
 */
int
msg_get_purpose(int fd)
{
	return clist_get_purpose(fd);
}


/**
  Get the flags of a file descriptor

  @param fd		File descriptor
  @return 		Purpose ID if successful, -1 on failure (fd not found
  			in our list.)
 */
int
msg_get_flags(int fd)
{
	return clist_get_flags(fd);
}


/**
  Look at a message without reading it off the socket.

  @param sockfd		File descriptor
  @param buf		Preallocated buffer to store received data.
  @param count		Maximum amount of data to read, in bytes.
  @return 		Number of bytes peeked, or -1 on failure.
  @see recv(2)
 */
ssize_t
msg_peek(int sockfd, void *buf, ssize_t count)
{

	if (sockfd < 0) {
		return -1;
	}

	return recv(sockfd, buf, count, MSG_PEEK);
}
