#ifndef _XVM_MCAST_H
#define _XVM_MCAST_H

#define IPV4_MCAST_DEFAULT "225.0.0.12"
#define IPV6_MCAST_DEFAULT "ff05::3:1"

int ipv4_recv_sk(char *addr, int port, unsigned int ifindex);
int ipv4_send_sk(char *src_addr, char *addr, int port,
		 struct sockaddr *src, socklen_t slen,
		 int ttl);
int ipv6_recv_sk(char *addr, int port, unsigned int ifindex);
int ipv6_send_sk(char *src_addr, char *addr, int port,
		 struct sockaddr *src, socklen_t slen,
		 int ttl);

#endif
