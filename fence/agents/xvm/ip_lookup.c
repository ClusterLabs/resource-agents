/** @file
 * Build lists of IPs on the system, excepting loopback ipv6 link-local
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

#ifndef IFA_MAX
#include <linux/if_addr.h>
#endif

/* Local includes */
#include "ip_lookup.h"
#include "debug.h"

LOGSYS_DECLARE_SUBSYS("XVM", LOG_LEVEL_INFO);

static int
send_addr_dump(int fd, int family)
{
	struct nlmsghdr *nh;
	struct rtgenmsg *g;
	char buf[256];
	struct sockaddr_nl addr;

	memset(&addr,0,sizeof(addr));
	addr.nl_family = PF_NETLINK;

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

	if (family == PF_INET6) {
		/* Avoid loopback */
		if (!strcmp(ipaddr, "::1"))
			return -1;

		/* Avoid link-local addresses */
		if (!strncmp(ipaddr, "fe80", 4))
			return -1;
		if (!strncmp(ipaddr, "fe90", 4))
			return -1;
		if (!strncmp(ipaddr, "fea0", 4))
			return -1;
		if (!strncmp(ipaddr, "feb0", 4))
			return -1;
	}
	
	dbg_printf(4, "Adding IP %s to list (family %d)\n", ipaddr, family);

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

	dbg_printf(5, "Connecting to Netlink...\n");
	fd = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
	if (fd < 0) {
		perror("socket");
		exit(1);
	}
	
	dbg_printf(5, "Sending address dump request\n");
	send_addr_dump(fd, family);
	memset(buf, 0, sizeof(buf));
	
	dbg_printf(5, "Waiting for response\n");
	x = recvfrom(fd, buf, sizeof(buf), 0, NULL, 0);
	if (x < 0) {
		perror("recvfrom");
		return -1;
	}
	
	dbg_printf(5, "Received %d bytes\n", x);

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
			if (!RTA_OK(rta, len)) {
				dbg_printf(5, "!RTA_OK(rta, len)\n");
				break;
			}

			if (rta->rta_type == IFA_ADDRESS) {
				inet_ntop(family, RTA_DATA(rta), outbuf,
					  sizeof(outbuf) );
				add_ip(ipl, outbuf, family);
			}

			if (rta->rta_type == IFA_LABEL) {
				dbg_printf(5, "Skipping label: %s\n",
					(char *)RTA_DATA(rta));
			}

			nrta = RTA_NEXT(rta, len);
			if (!nrta)
				break;

			len -= ((void *)nrta - (void *)rta);
			rta = nrta;
		} while (RTA_OK(rta, len));

		nh = NLMSG_NEXT(nh, x);
	}

	dbg_printf(5, "Closing Netlink connection\n");
	close(fd);
	return 0;
}


int
ip_search(ip_list_t *ipl, char *ip_name)
{
	ip_addr_t *ipa;
	
	dbg_printf(5, "Looking for IP address %s in IP list %p...", ip_name, ipl);
	ipa = ipl->tqh_first;
	for (ipa = ipl->tqh_first; ipa; ipa = ipa->ipa_entries.tqe_next) {
		if (!strcmp(ip_name, ipa->ipa_address)) {
			dbg_printf(4,"Found\n");
			return 0;
		}
	}
	dbg_printf(5, "Not found\n");
	return 1;
}


int
ip_free_list(ip_list_t *ipl)
{
	ip_addr_t *ipa;
	
	dbg_printf(5, "Tearing down IP list @ %p\n", ipl);
	while ((ipa = ipl->tqh_first)) {
		TAILQ_REMOVE(ipl, ipa, ipa_entries);
		free(ipa->ipa_address);
		free(ipa);
	}
	return 0;
}


int
ip_build_list(ip_list_t *ipl)
{
	dbg_printf(5, "Build IP address list\n");
	TAILQ_INIT(ipl);
	if (add_ip_addresses(PF_INET6, ipl) < 0) {
		ip_free_list(ipl);
		return -1;
	}
	if (add_ip_addresses(PF_INET, ipl) < 0) {
		ip_free_list(ipl);
		return -1;
	}
	return 0;
}


/**
  Look up the interface name which corresponds to the given hostname and
  return the list of matching attrinfo structures.  We do this by looking
  up all the possible physical and virtual network interfaces on the machine
  and checking the hostname/IP mappings for each active IP address incurred.

  @param nodename	Interface name
  @param ret_ai		Structure pointer to allocate & return.
  @return		-1 on failure or 0 on success.
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

	dbg_printf(5, "Looking for IP matching %s\n", nodename);
	/* Build list of IP addresses configured locally */
	if (ip_build_list(&ipl) < 0)
		return -1;

	/* Get list of addresses for the host-name/ip */
	if (getaddrinfo(nodename, NULL, NULL, &ai) != 0) 
		return -1;
	

	/* Traverse list of addresses for given host-name/ip */
	for (n = ai; n; n = n->ai_next) {
		if (n->ai_family != PF_INET && n->ai_family != PF_INET6)
			continue;

		if (n->ai_family == PF_INET)
			p = &(((struct sockaddr_in *)n->ai_addr)->sin_addr);
		else
			p = &(((struct sockaddr_in6 *)n->ai_addr)->sin6_addr);

		if (!inet_ntop(n->ai_family, p, ip_name,
			       sizeof(ip_name)))
			continue;

		/* Search local interfaces for this IP address */
		if (ip_search(&ipl, ip_name) != 0)
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

	ip_free_list(&ipl);

	return ret;
}

