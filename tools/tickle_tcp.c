/* 
   Tickle TCP connections tool

   Author:	Jiaju Zhang
   Based on the code in CTDB http://ctdb.samba.org/ written by
   Andrew Tridgell and Ronnie Sahlberg

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <net/if.h>

#define discard_const(ptr) ((void *)((intptr_t)(ptr)))

typedef union {
	struct sockaddr     sa;
	struct sockaddr_in  ip;
	struct sockaddr_in6 ip6;
} sock_addr;

uint32_t uint16_checksum(uint16_t *data, size_t n);
void set_nonblocking(int fd);
void set_close_on_exec(int fd);
static int parse_ipv4(const char *s, unsigned port, struct sockaddr_in *sin);
static int parse_ipv6(const char *s, const char *iface, unsigned port, sock_addr *saddr);
int parse_ip(const char *addr, const char *iface, unsigned port, sock_addr *saddr);
int parse_ip_port(const char *addr, sock_addr *saddr);
int send_tickle_ack(const sock_addr *dst, 
		    const sock_addr *src, 
		    uint32_t seq, uint32_t ack, int rst);
static void usage(void);

uint32_t uint16_checksum(uint16_t *data, size_t n)
{
	uint32_t sum=0;
	while (n >= 2) {
		sum += (uint32_t)ntohs(*data);
		data++;        
		n -= 2;
	}                      
	if (n == 1) {
		sum += (uint32_t)ntohs(*(uint8_t *)data);
	}
	return sum;
}       

static uint16_t tcp_checksum(uint16_t *data, size_t n, struct iphdr *ip)
{
	uint32_t sum = uint16_checksum(data, n);
	uint16_t sum2;
	sum += uint16_checksum((uint16_t *)(void *)&ip->saddr,
				sizeof(ip->saddr));
	sum += uint16_checksum((uint16_t *)(void *)&ip->daddr,
				sizeof(ip->daddr));
	sum += ip->protocol + n;
	sum = (sum & 0xFFFF) + (sum >> 16);
	sum = (sum & 0xFFFF) + (sum >> 16);
	sum2 = htons(sum);
	sum2 = ~sum2;
	if (sum2 == 0) {
		return 0xFFFF;
	}
	return sum2;
}

static uint16_t tcp_checksum6(uint16_t *data, size_t n, struct ip6_hdr *ip6)
{
	uint32_t phdr[2];
	uint32_t sum = 0;
	uint16_t sum2;

	sum += uint16_checksum((uint16_t *)(void *)&ip6->ip6_src, 16);
	sum += uint16_checksum((uint16_t *)(void *)&ip6->ip6_dst, 16);

	phdr[0] = htonl(n);
	phdr[1] = htonl(ip6->ip6_nxt);
	sum += uint16_checksum((uint16_t *)phdr, 8);

	sum += uint16_checksum(data, n);

	sum = (sum & 0xFFFF) + (sum >> 16);
	sum = (sum & 0xFFFF) + (sum >> 16);
	sum2 = htons(sum);
	sum2 = ~sum2;
	if (sum2 == 0) {
		return 0xFFFF;
	}
	return sum2;
}

void set_nonblocking(int fd)
{
	unsigned v;
	v = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, v | O_NONBLOCK);
}

void set_close_on_exec(int fd) 
{               
	unsigned v;
	v = fcntl(fd, F_GETFD, 0);
	fcntl(fd, F_SETFD, v | FD_CLOEXEC);
}

static int parse_ipv4(const char *s, unsigned port, struct sockaddr_in *sin)
{
	sin->sin_family = AF_INET;
	sin->sin_port   = htons(port);

	if (inet_pton(AF_INET, s, &sin->sin_addr) != 1) {
		fprintf(stderr, "Failed to translate %s into sin_addr\n", s);
		return -1;
	}

	return 0;
}

static int parse_ipv6(const char *s, const char *iface, unsigned port, sock_addr *saddr)
{
	saddr->ip6.sin6_family   = AF_INET6;
	saddr->ip6.sin6_port     = htons(port);
	saddr->ip6.sin6_flowinfo = 0;
	saddr->ip6.sin6_scope_id = 0;

	if (inet_pton(AF_INET6, s, &saddr->ip6.sin6_addr) != 1) {
		fprintf(stderr, "Failed to translate %s into sin6_addr\n", s);
		return -1;
	}

	if (iface && IN6_IS_ADDR_LINKLOCAL(&saddr->ip6.sin6_addr)) {
		saddr->ip6.sin6_scope_id = if_nametoindex(iface);
	}

        return 0;
}

int parse_ip(const char *addr, const char *iface, unsigned port, sock_addr *saddr)
{
	char *p;
	int ret;

	p = index(addr, ':');
	if (!p)
		ret = parse_ipv4(addr, port, &saddr->ip);
	else
		ret = parse_ipv6(addr, iface, port, saddr);

	return ret;
}

int parse_ip_port(const char *addr, sock_addr *saddr)
{
	char *s, *p;
	unsigned port;
	char *endp = NULL;
	int ret;

	s = strdup(addr);
	if (!s) {
		fprintf(stderr, "Failed strdup()\n");
		return -1;
	}

	p = rindex(s, ':');
	if (!p) {
		fprintf(stderr, "This addr: %s does not contain a port number\n", s);
		free(s);
		return -1;
	}
	
	port = strtoul(p+1, &endp, 10);
	if (!endp || *endp != 0) {
		fprintf(stderr, "Trailing garbage after the port in %s\n", s);
		free(s);
		return -1;
	}
	*p = 0;

	ret = parse_ip(s, NULL, port, saddr);
	free(s);
	return ret;
}

int send_tickle_ack(const sock_addr *dst, 
		    const sock_addr *src, 
		    uint32_t seq, uint32_t ack, int rst)
{
	int s;
	int ret;
	uint32_t one = 1;
	uint16_t tmpport;
	sock_addr *tmpdest;
	struct {
		struct iphdr ip;
		struct tcphdr tcp;
	} ip4pkt;
	struct {
		struct ip6_hdr ip6;
		struct tcphdr tcp;
	} ip6pkt;

	switch (src->ip.sin_family) {
	case AF_INET:
		memset(&ip4pkt, 0, sizeof(ip4pkt));
		ip4pkt.ip.version  = 4;
		ip4pkt.ip.ihl      = sizeof(ip4pkt.ip)/4;
		ip4pkt.ip.tot_len  = htons(sizeof(ip4pkt));
		ip4pkt.ip.ttl      = 255;
		ip4pkt.ip.protocol = IPPROTO_TCP;
		ip4pkt.ip.saddr    = src->ip.sin_addr.s_addr;
		ip4pkt.ip.daddr    = dst->ip.sin_addr.s_addr;
		ip4pkt.ip.check    = 0;

		ip4pkt.tcp.source  = src->ip.sin_port;
		ip4pkt.tcp.dest    = dst->ip.sin_port;
		ip4pkt.tcp.seq     = seq;
		ip4pkt.tcp.ack_seq = ack;
		ip4pkt.tcp.ack     = 1;
		if (rst)
			ip4pkt.tcp.rst = 1;
		ip4pkt.tcp.doff    = sizeof(ip4pkt.tcp)/4;
		ip4pkt.tcp.window   = htons(1234);
		ip4pkt.tcp.check    = tcp_checksum((uint16_t *)&ip4pkt.tcp, sizeof(ip4pkt.tcp), &ip4pkt.ip);

		s = socket(AF_INET, SOCK_RAW, htons(IPPROTO_RAW));
		if (s == -1) {
			fprintf(stderr, "Failed to open raw socket (%s)\n", strerror(errno));
			return -1;
		}

		ret = setsockopt(s, SOL_IP, IP_HDRINCL, &one, sizeof(one));
		if (ret != 0) {
			fprintf(stderr, "Failed to setup IP headers (%s)\n", strerror(errno));
			close(s);
			return -1;
		}

		set_nonblocking(s);
		set_close_on_exec(s);

		ret = sendto(s, &ip4pkt, sizeof(ip4pkt), 0, 
			     (const struct sockaddr *)&dst->ip, sizeof(dst->ip));
		close(s);
		if (ret != sizeof(ip4pkt)) {
			fprintf(stderr, "Failed sendto (%s)\n", strerror(errno));
			return -1;
		}
		break;

        case AF_INET6:
		memset(&ip6pkt, 0, sizeof(ip6pkt));
		ip6pkt.ip6.ip6_vfc  = 0x60;
		ip6pkt.ip6.ip6_plen = htons(20);
		ip6pkt.ip6.ip6_nxt  = IPPROTO_TCP;
		ip6pkt.ip6.ip6_hlim = 64;
		ip6pkt.ip6.ip6_src  = src->ip6.sin6_addr;
		ip6pkt.ip6.ip6_dst  = dst->ip6.sin6_addr;

		ip6pkt.tcp.source   = src->ip6.sin6_port;
		ip6pkt.tcp.dest     = dst->ip6.sin6_port;
		ip6pkt.tcp.seq      = seq;
		ip6pkt.tcp.ack_seq  = ack;
		ip6pkt.tcp.ack      = 1;
		if (rst)
			ip6pkt.tcp.rst      = 1;
		ip6pkt.tcp.doff     = sizeof(ip6pkt.tcp)/4;
		ip6pkt.tcp.window   = htons(1234);
		ip6pkt.tcp.check    = tcp_checksum6((uint16_t *)&ip6pkt.tcp, sizeof(ip6pkt.tcp), &ip6pkt.ip6);

		s = socket(PF_INET6, SOCK_RAW, IPPROTO_RAW);
		if (s == -1) {
			fprintf(stderr, "Failed to open sending socket\n");
			return -1;
                }

		tmpdest = discard_const(dst);
		tmpport = tmpdest->ip6.sin6_port;

		tmpdest->ip6.sin6_port = 0;
		ret = sendto(s, &ip6pkt, sizeof(ip6pkt), 0, (const struct sockaddr *)&dst->ip6, sizeof(dst->ip6));
		tmpdest->ip6.sin6_port = tmpport;
		close(s);

		if (ret != sizeof(ip6pkt)) {
			fprintf(stderr, "Failed sendto (%s)\n", strerror(errno));
			return -1;
		}
		break;

	default:
		fprintf(stderr, "Not an ipv4/v6 address\n");
		return -1;
	}

	return 0;
}

static void usage(void)
{
	printf("Usage: /usr/lib/heartbeat/tickle_tcp [ -n num ]\n");
	printf("Please note that this program need to read the list of\n");
	printf("{local_ip:port remote_ip:port} from stdin.\n");
	exit(1);
}

#define OPTION_STRING "n:h"

int main(int argc, char *argv[])
{
	int optchar, i, num = 1, cont = 1;
	sock_addr src, dst;
	char addrline[128], addr1[64], addr2[64];

	while(cont) {
		optchar = getopt(argc, argv, OPTION_STRING);
		switch(optchar) {
		case 'n':
			num = atoi(optarg);
			break;
		case 'h':
			usage();
			exit(EXIT_SUCCESS);
			break;
		case EOF:
			cont = 0;
			break;
		default:
			fprintf(stderr, "unknown option, please use '-h' for usage.\n");
			exit(EXIT_FAILURE);
			break;
		};
	}

	while(fgets(addrline, sizeof(addrline), stdin)) {
		sscanf(addrline, "%s %s", addr1, addr2);

		if (parse_ip_port(addr1, &src)) {
			fprintf(stderr, "Bad IP:port '%s'\n", addr1);
			return -1;
		}
		if (parse_ip_port(addr2, &dst)) {
			fprintf(stderr, "Bad IP:port '%s'\n", addr2);
			return -1;
		}
	
		for (i = 1; i <= num; i++) {
			if (send_tickle_ack(&dst, &src, 0, 0, 0)) {
				fprintf(stderr, "Error while sending tickle ack from '%s' to '%s'\n",
					addr1, addr2);
				return -1;
			}
		}

	}
	return 0;
}
