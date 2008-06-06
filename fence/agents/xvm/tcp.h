#ifndef _XVM_TCP_H
#define _XVM_TCP_H

int ipv4_connect(struct in_addr *in_addr, uint16_t port, int timeout);
int ipv6_connect(struct in6_addr *in6_addr, uint16_t port, int timeout);
int ipv4_listen(uint16_t port, int backlog);
int ipv6_listen(uint16_t port, int backlog);

#endif
