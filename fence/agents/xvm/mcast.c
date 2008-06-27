/*
 * Author: Lon Hohberger <lhh at redhat.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

/* Local includes */
#include "mcast.h"
#include "debug.h"

LOGSYS_DECLARE_SUBSYS ("XVM", LOG_LEVEL_NOTICE);

/** 
  Sets up a multicast receive socket
 */
int
ipv4_recv_sk(char *addr, int port)
{
	int sock;
	struct ip_mreq mreq;
	struct sockaddr_in sin;

	/* Store multicast address */
	if (inet_pton(PF_INET, addr,
		      (void *)&mreq.imr_multiaddr.s_addr) < 0) {
		printf("Invalid multicast address: %s\n", addr);
		return -1;
	}

	/********************************
	 * SET UP MULTICAST RECV SOCKET *
	 ********************************/
	dbg_printf(4, "Setting up ipv4 multicast receive (%s:%d)\n", addr, port);
	sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		printf("socket: %s\n", strerror(errno));
		close(sock);
		sock = -1;
		return 1;
	}

	/*
	 * When using Multicast, bind to the LOCAL address, not the MULTICAST
	 * address.
	 */
	sin.sin_family = PF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(sock, (struct sockaddr *) &sin,
		 sizeof(struct sockaddr_in)) < 0) {
		printf("bind failed: %s\n", strerror(errno));
		close(sock);
		return -1;
	}

	/*
	 * Join multicast group
	 */
	/* mreq.imr_multiaddr.s_addr is set above */
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	dbg_printf(4, "Joining multicast group\n");
	if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
		       &mreq, sizeof(mreq)) == -1) {
		printf("Failed to bind multicast receive socket to "
		       "%s: %s\n", addr, strerror(errno));
		printf("Check network configuration.\n");
		close(sock);
		return -1;
	}

	dbg_printf(4, "%s: success, fd = %d\n", __FUNCTION__, sock);
	return sock;
}


/**
  Set up multicast send socket
 */
int
ipv4_send_sk(char *send_addr, char *addr, int port, struct sockaddr *tgt,
	     socklen_t tgt_len, int ttl)
{
	int val;
	struct ip_mreq mreq;
	struct sockaddr_in mcast;
	struct sockaddr_in src;
	int sock;

	if (tgt_len < sizeof(struct sockaddr_in)) {
		errno = EINVAL;
		return -1;
	}

	/* Store multicast address */
	mcast.sin_family = PF_INET;
	mcast.sin_port = htons(port);
	if (inet_pton(PF_INET, addr,
		      (void *)&mcast.sin_addr.s_addr) < 0) {
		printf("Invalid multicast address: %s\n", addr);
		return -1;
	}
	mreq.imr_multiaddr.s_addr = mcast.sin_addr.s_addr;

	/* Store sending address */
	src.sin_family = PF_INET;
	src.sin_port = htons(port);
	if (inet_pton(PF_INET, send_addr,
		      (void *)&src.sin_addr.s_addr) < 0) {
		printf("Invalid source address: %s\n", send_addr);
		return -1;
	}
	mreq.imr_interface.s_addr = src.sin_addr.s_addr;


	/*************************
	 * SET UP MULTICAST SEND *
	 *************************/
	dbg_printf(4, "Setting up ipv4 multicast send (%s:%d)\n", addr, port);
	sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		perror("socket");
		return -1;
	}

	/*
	 * Join Multicast group.
	 */
	dbg_printf(4, "Joining IP Multicast group (pass 1)\n");
	if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq,
		       sizeof(mreq)) == -1) {
		printf("Failed to add multicast membership to transmit "
		       "socket %s: %s\n", addr, strerror(errno));
		close(sock);
		return -1;
	}

	/*
	 * Join Multicast group.
	 */
	dbg_printf(4, "Joining IP Multicast group (pass 2)\n");
	if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &src.sin_addr,
		       sizeof(src.sin_addr)) == -1) {
		printf("Failed to bind multicast transmit socket to "
		       "%s: %s\n", addr, strerror(errno));
		close(sock);
		return -1;
	}

	/*
	 * set time to live to 2 hops.
	 */
	dbg_printf(4, "Setting TTL to %d for fd%d\n", ttl, sock);
	val = ttl;
	if (setsockopt(sock, SOL_IP, IP_MULTICAST_TTL, &val,
		       sizeof(val)))
		printf("warning: setting TTL failed %s\n", strerror(errno));

	memcpy((struct sockaddr_in *)tgt, &mcast, sizeof(struct sockaddr_in));

	dbg_printf(4, "%s: success, fd = %d\n", __FUNCTION__, sock);
	return sock;
}



/** 
  Sets up a multicast receive (ipv6) socket
 */
int
ipv6_recv_sk(char *addr, int port)
{
	int sock, val;
	struct ipv6_mreq mreq;
	struct sockaddr_in6 sin;

	memset(&mreq, 0, sizeof(mreq));
	memset(&sin, 0, sizeof(sin));
	sin.sin6_family = PF_INET6;
	sin.sin6_port = htons(port);
	if (inet_pton(PF_INET6, addr,
		      (void *)&sin.sin6_addr) < 0) {
		printf("Invalid multicast address: %s\n", addr);
		return -1;
	}

	memcpy(&mreq.ipv6mr_multiaddr, &sin.sin6_addr,
	       sizeof(struct in6_addr));


	/********************************
	 * SET UP MULTICAST RECV SOCKET *
	 ********************************/
	dbg_printf(4, "Setting up ipv6 multicast receive (%s:%d)\n", addr, port);
	sock = socket(PF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		printf("socket: %s\n", strerror(errno));
		close(sock);
		sock = -1;
		return 1;
	}

	/*
	 * When using Multicast, bind to the LOCAL address, not the MULTICAST
	 * address.
	 */
	memset(&sin, 0, sizeof(sin));
	sin.sin6_family = PF_INET6;
	sin.sin6_port = htons(port);
	sin.sin6_addr = in6addr_any;
	if (bind(sock, (struct sockaddr *) &sin,
		 sizeof(struct sockaddr_in6)) < 0) {
		printf("bind failed: %s\n", strerror(errno));
		close(sock);
		return -1;
	}

	dbg_printf(4, "Disabling IP Multicast loopback\n");
	val = 1;
	if (setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &val,
		       sizeof(val)) != 0) {
		printf("Failed to disable multicast loopback\n");
		close(sock);
		return -1;
	}

	/*
	 * Join multicast group
	 */
	dbg_printf(4, "Joining IP Multicast group\n");
	if (setsockopt(sock, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &mreq,
		       sizeof(mreq)) == -1) {
		printf("Failed to add multicast to socket %s: %s\n",
		       addr, strerror(errno));
		close(sock);
		return -1;
	}

	dbg_printf(4, "%s: success, fd = %d\n", __FUNCTION__, sock);
	return sock;
}


/**
  Set up ipv6 multicast send socket
 */
int
ipv6_send_sk(char *send_addr, char *addr, int port, struct sockaddr *tgt,
	     socklen_t tgt_len, int ttl)
{
	int val;
	struct ipv6_mreq mreq;
	struct sockaddr_in6 mcast;
	struct sockaddr_in6 src;
	int sock;

	if (tgt_len < sizeof(struct sockaddr_in6)) {
		errno = EINVAL;
		return -1;
	}

	memset(&mreq, 0, sizeof(mreq));

	/* Store multicast address */
	mcast.sin6_family = PF_INET6;
	mcast.sin6_port = htons(port);
	if (inet_pton(PF_INET6, addr,
		      (void *)&mcast.sin6_addr) < 0) {
		printf("Invalid multicast address: %s\n", addr);
		return -1;
	}

	memcpy(&mreq.ipv6mr_multiaddr, &mcast.sin6_addr,
	       sizeof(struct in6_addr));

	/* Store sending address */
	src.sin6_family = PF_INET6;
	src.sin6_port = htons(port);
	if (inet_pton(PF_INET6, send_addr,
		      (void *)&src.sin6_addr) < 0) {
		printf("Invalid source address: %s\n", send_addr);
		return -1;
	}

	/*************************
	 * SET UP MULTICAST SEND *
	 *************************/
	dbg_printf(4, "Setting up ipv6 multicast send (%s:%d)\n", addr, port);
	sock = socket(PF_INET6, SOCK_DGRAM, 0);
	if (sock < 0) {
		perror("socket");
		return -1;
	}

	dbg_printf(4, "Disabling IP Multicast loopback\n");
	val = 1;
	if (setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &val,
		       sizeof(val)) != 0) {
		printf("Failed to disable multicast loopback\n");
		close(sock);
		return -1;
	}

	/*
	 * Join Multicast group.
	 */
	dbg_printf(4, "Joining IP Multicast group\n");
	if (setsockopt(sock, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &mreq,
		       sizeof(mreq)) == -1) {
		printf("Failed to add multicast membership to transmit "
		       "socket %s: %s\n", addr, strerror(errno));
		close(sock);
		return -1;
	}

	/*
	 * Join Multicast group (part 2)
	 */
	/*
	if (setsockopt(sock, IPPROTO_IPV6, IP_MULTICAST_IF, &src.sin6_addr,
		       sizeof(src.sin6_addr)) == -1) {
		printf("Failed to bind multicast transmit socket to "
		       "%s: %s\n", addr, strerror(errno));
		close(sock);
		return -1;
	}
	*/

	/*
	 * set time to live to 2 hops.
	 */
	val = ttl;
	if (setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &val,
		       sizeof(val)))
		printf("warning: setting TTL failed %s\n", strerror(errno));

	memcpy((struct sockaddr_in *)tgt, &mcast, sizeof(struct sockaddr_in6));

	dbg_printf(4, "%s: success, fd = %d\n", __FUNCTION__, sock);
	return sock;
}
