/*
  Copyright Red Hat, Inc. 2004

  The Magma Cluster API Library is free software; you can redistribute
  it and/or modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either version
  2.1 of the License, or (at your option) any later version.

  The Magma Cluster API Library is distributed in the hope that it will
  be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
  of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
 */
/** @file
  replacement for if_lookup using netlink instead of ioctls
 */
#include <asm/types.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <netdb.h>
#include <sys/queue.h>

#ifdef MDEBUG
#include <mallocdbg.h>
#endif

typedef struct __ip_address {
	TAILQ_ENTRY(__ip_address) ipa_entries;
	char ipa_family;
	char *ipa_address;
} ip_addr_t;

typedef TAILQ_HEAD(__ip_list, __ip_address) ip_list_t;


static int
send_addr_dump(int fd, int family)
{
	struct nlmsghdr *nh;
	struct rtgenmsg *g;
	char buf[256];
	struct sockaddr_nl addr;

	memset(&addr,0,sizeof(addr));
	addr.nl_family = AF_NETLINK;

	memset(buf, 0, sizeof(buf));
	nh = (struct nlmsghdr *)buf;
	g = (struct rtgenmsg *)(buf + sizeof(struct nlmsghdr));

	nh->nlmsg_len = NLMSG_LENGTH(sizeof(struct rtgenmsg));
	nh->nlmsg_flags = NLM_F_REQUEST|NLM_F_DUMP;
	nh->nlmsg_type = RTM_GETADDR;
	g->rtgen_family = family;

	return sendto(fd, buf, nh->nlmsg_len, 0, (struct sockaddr *)&addr,
	   	      sizeof(addr));
}


static int
add_ip(ip_list_t *ipl, char *ipaddr, char family)
{
	ip_addr_t *ipa;

	ipa = malloc(sizeof(*ipa));
	memset(ipa, 0, sizeof(*ipa));
	ipa->ipa_family = family;
	ipa->ipa_address = strdup(ipaddr);

	TAILQ_INSERT_TAIL(ipl, ipa, ipa_entries);

	return 0;
}


static int
add_ip_addresses(int family, ip_list_t *ipl)
{
	/* List ipv4 addresses */
	struct nlmsghdr *nh;
	struct ifaddrmsg *ifa;
	struct rtattr *rta, *nrta;
	struct nlmsgerr *err;
	char buf[10240];
	char outbuf[256];
	int x, fd, len;

	fd = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
	if (fd < 0) {
		perror("socket");
		exit(1);
	}

	send_addr_dump(fd, family);
	memset(buf, 0, sizeof(buf));
	x = recvfrom(fd, buf, sizeof(buf), 0, NULL, 0);
	if (x < 0) {
		perror("recvfrom");
		return -1;
	}

	nh = (struct nlmsghdr *)buf;
	while (NLMSG_OK(nh, x)) {

		switch(nh->nlmsg_type) {
		case NLMSG_DONE:
			close(fd);
    			return 0;

		case NLMSG_ERROR:
			err = (struct nlmsgerr*)NLMSG_DATA(nh);
			if (nh->nlmsg_len <
			    NLMSG_LENGTH(sizeof(struct nlmsgerr))) {
				fprintf(stderr, "ERROR truncated");
			} else {
				errno = -err->error;
				perror("RTNETLINK answers");
			}
			close(fd);
			return -1;

		case RTM_NEWADDR:
			break;

		default:
			nh = NLMSG_NEXT(nh, x);
			continue;
		}

		/* RTM_NEWADDR */
		len = NLMSG_PAYLOAD(nh,0);
		ifa = NLMSG_DATA(nh);

		/* Make sure we got the type we expect back */
		if (ifa->ifa_family != family) {
			nh = NLMSG_NEXT(nh, x);
			continue;
		}

		rta = (struct rtattr *)((void *)ifa + sizeof(*ifa));
		len -= sizeof(*ifa);
		do {
			/* Make sure we've got a valid rtaddr field */
			if (!RTA_OK(rta, len))
				break;

			if (rta->rta_type == IFA_ADDRESS ||
			    rta->rta_type == IFA_BROADCAST) {
				inet_ntop(family, RTA_DATA(rta), outbuf,
					  sizeof(outbuf) );
				add_ip(ipl, outbuf, family);
			}

			if (rta->rta_type == IFA_LABEL) {
				printf("label: %s\n", (char *)RTA_DATA(rta));
			}

			nrta = RTA_NEXT(rta, len);
			if (!nrta)
				break;

			len -= ((void *)nrta - (void *)rta);
			rta = nrta;
		} while (RTA_OK(rta, len));

		nh = NLMSG_NEXT(nh, x);
	}

	close(fd);
	return 0;
}


static int
search_ip_list(ip_list_t *ipl, char *ip_name)
{
	ip_addr_t *ipa;

	ipa = ipl->tqh_first;
	for (ipa = ipl->tqh_first; ipa; ipa = ipa->ipa_entries.tqe_next) {
		if (!strcmp(ip_name, ipa->ipa_address)) {
			return 0;
		}
	}
	return 1;
}


static inline int
free_ip_list(ip_list_t *ipl)
{
	ip_addr_t *ipa;

	while ((ipa = ipl->tqh_first)) {
		TAILQ_REMOVE(ipl, ipa, ipa_entries);
		free(ipa->ipa_address);
		free(ipa);
	}
	return 0;
}


static inline int
build_ip_list(ip_list_t *ipl)
{
	if (add_ip_addresses(AF_INET6, ipl) < 0) {
		free_ip_list(ipl);
		return -1;
	}
	if (add_ip_addresses(AF_INET, ipl) < 0) {
		free_ip_list(ipl);
		return -1;
	}
	return 0;
}


/**
 * 
 */
int
ip_lookup(char *nodename, struct addrinfo **ret_ai)
{
	char ip_name[256];
	struct addrinfo *ai = NULL;
	struct addrinfo *n;
	void *p;
	ip_list_t ipl;
	int ret = -1;

	/* Build list of IP addresses configured locally */
	TAILQ_INIT(&ipl);
	if (build_ip_list(&ipl) < 0)
		return -1;

	/* Get list of addresses for the host-name/ip */
	if (getaddrinfo(nodename, NULL, NULL, &ai) != 0) 
		return -1;

	/* Traverse list of addresses for given host-name/ip */
	for (n = ai; n; n = n->ai_next) {
		if (n->ai_family != AF_INET && n->ai_family != AF_INET6)
			continue;

		if (n->ai_family == AF_INET)
			p = &(((struct sockaddr_in *)n->ai_addr)->sin_addr);
		else
			p = &(((struct sockaddr_in6 *)n->ai_addr)->sin6_addr);

		if (!inet_ntop(n->ai_family, p, ip_name,
			       sizeof(ip_name)))
			continue;

		/* Search local interfaces for this IP address */
		if (search_ip_list(&ipl, ip_name) != 0)
			continue;

		/* Found it */
		ret = 0;
		break;
	}

	/* Clean up */
	if (!ret_ai)
		freeaddrinfo(ai);
	else
		*ret_ai = ai;

	free_ip_list(&ipl);

	return ret;
}

