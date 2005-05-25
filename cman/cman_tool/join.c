/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <net/route.h>
#include "libcman.h"
#include "cman_tool.h"

static char *argv[128];

/* Lookup the IPv4 broadcast address for a given local address */
static uint32_t lookup_bcast(uint32_t localaddr, char *ifname)
{
	struct ifreq *ifr;
	struct ifconf ifc;
	uint32_t addr, brdaddr;
	int n;
	int numreqs = 30;
	int sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	struct sockaddr_in *saddr;

	ifc.ifc_buf = NULL;
	for (;;) {
		ifc.ifc_len = sizeof(struct ifreq) * numreqs;
		ifc.ifc_buf = realloc(ifc.ifc_buf, ifc.ifc_len);

		if (ioctl(sock, SIOCGIFCONF, &ifc) < 0) {
			die("SIOCGIFCONF failed: %s", strerror(errno));
		}
		if (ifc.ifc_len == sizeof(struct ifreq) * numreqs) {
			/* assume it overflowed and try again */
			numreqs += 10;
			continue;
		}
		break;
	}

	ifr = ifc.ifc_req;
	for (n = 0; n < ifc.ifc_len; n += sizeof(struct ifreq)) {

		strcpy(ifname, ifr->ifr_name);

		ioctl(sock, SIOCGIFADDR, ifr);
		saddr = (struct sockaddr_in *)&ifr->ifr_ifru.ifru_addr;
		addr = saddr->sin_addr.s_addr;

		if (addr == localaddr) {
			ioctl(sock, SIOCGIFBRDADDR, ifr);
			brdaddr = saddr->sin_addr.s_addr;

			close(sock);
			return brdaddr;
		}
		ifr++;
	}

	/* Didn't find it */
	close(sock);
	return 0;
}

/* Set the socket priority to INTERACTIVE to ensure
   that our messages don't get queued behind anything else */
static void set_priority(int sock)
{
	int prio = 6; /* TC_PRIO_INTERACTIVE */

	if (setsockopt(sock, SOL_SOCKET, SO_PRIORITY, &prio, sizeof(int)))
		perror("Error setting socket priority");
}

static void add_argv_sock(int num, int local_sock, int mcast_sock)
{
	char arg[128];

	sprintf(arg, "-s%d,%d", mcast_sock, local_sock);

	argv[num] = strdup(arg);
}

static int get_mcast_address(char *name, int family, struct sockaddr *saddr)
{
	struct addrinfo *ainfo;
	struct addrinfo ahints;
	int ret;

	memset(&ahints, 0, sizeof(ahints));
	ahints.ai_socktype = SOCK_DGRAM;
	ahints.ai_protocol = IPPROTO_UDP;
	ahints.ai_family   = family;

	/* Lookup the multicast address */
	ret = getaddrinfo(name, NULL, &ahints, &ainfo);
	if (ret)
		die("can't resolve multicast address %s: %s\n", name, gai_strerror(ret));

	if (ainfo->ai_next)
		die("multicast address %s is ambiguous\n", name);

	memcpy(saddr, ainfo->ai_addr, ainfo->ai_addrlen);
	freeaddrinfo(ainfo);
	return 0;
}

static int setup_ipv4_interface(commandline_t *comline, int num, struct sockaddr *sa)
{
	struct sockaddr_in mcast_sin;
	struct sockaddr_in local_sin;
	struct sockaddr_in *sin = (struct sockaddr_in *)sa;
	int mcast_sock;
	int local_sock;
	uint32_t bcast=0;

	memset(&mcast_sin, 0, sizeof(mcast_sin));
	mcast_sin.sin_family = AF_INET;
	mcast_sin.sin_port = htons(comline->port);

	memset(&local_sin, 0, sizeof(local_sin));
	memcpy(&local_sin, sa, sizeof(local_sin));
	local_sin.sin_family = AF_INET;
	local_sin.sin_port = htons(comline->port);

	if (comline->verbose)
		printf("setup up interface for address: %s\n", comline->nodenames[num]);

	if (!comline->multicast_names[num]) {
		uint32_t ipaddr;
		char ifname[256];

		memcpy(&ipaddr, &sin->sin_addr, sizeof(uint32_t));
		bcast = lookup_bcast(ipaddr, ifname);
		if (!bcast) {
			fprintf(stderr, "%s: Can't find broadcast address for node name \"%s\"\n", prog_name, comline->nodenames[num]);
			if (ifname[0])
				fprintf(stderr, "%s: Interface \"%s\" was bound to that hostname\n", prog_name, ifname);
			exit(EXIT_FAILURE);
		}

		if (comline->verbose) {
			printf("Broadcast address for %x is %x\n", ipaddr, bcast);
		}
	}
	else {
		uint32_t addr;
		get_mcast_address(comline->multicast_names[num], AF_INET, (struct sockaddr *)&mcast_sin);

		/* Check it really is a multicast address */
		memcpy(&addr, &mcast_sin.sin_addr, sizeof(uint32_t));
		if ((ntohl(addr) & 0xe0000000) != 0xe0000000)
			die("%s is not an IPv4 multicast address\n", comline->multicast_names[num]);

		mcast_sin.sin_port = htons(comline->port);
	}

	mcast_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (mcast_sock < 0)
		die("Can't open multicast socket: %s", strerror(errno));

	if (bcast) {
		/* Broadcast */
		int one=1;
		memcpy(&mcast_sin.sin_addr, &bcast, sizeof(struct in_addr));
		if (setsockopt(mcast_sock, SOL_SOCKET, SO_BROADCAST, (void *)&one, sizeof(int)))
			die("Can't enable broadcast: %s", strerror(errno));
	}
	if (bind(mcast_sock, (struct sockaddr *)&mcast_sin, sizeof(mcast_sin)))
		die("Cannot bind multicast address: %s", strerror(errno));

	/* Join the multicast group */
	if (!bcast) {
		struct ip_mreq mreq;
		char mcast_opt;

		memcpy(&mreq.imr_multiaddr, &mcast_sin.sin_addr, sizeof(uint32_t));
		memcpy(&mreq.imr_interface, &sin->sin_addr, sizeof(uint32_t));
		if (setsockopt(mcast_sock, SOL_IP, IP_ADD_MEMBERSHIP, (void *)&mreq, sizeof(mreq)))
			die("Unable to join multicast group %s: %s\n", comline->multicast_names[num], strerror(errno));

		mcast_opt = 10;
		if (setsockopt(mcast_sock, SOL_IP, IP_MULTICAST_TTL, (void *)&mcast_opt, sizeof(mcast_opt)))
			die("Unable to set ttl for multicast group %s: %s\n", comline->multicast_names[num], strerror(errno));

		mcast_opt = 0;
		setsockopt(mcast_sock, SOL_IP, IP_MULTICAST_LOOP, (void *)&mcast_opt, sizeof(mcast_opt));
	}

	/* Local socket */
	local_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (local_sock < 0)
		die("Can't open local socket: %s", strerror(errno));

	if (bind(local_sock, (struct sockaddr *)&local_sin, sizeof(local_sin)))
		die("Cannot bind local address: %s", strerror(errno));

	set_priority(mcast_sock);
	set_priority(local_sock);

	add_argv_sock(num+1, local_sock, mcast_sock);

	return 0;
}


static int setup_ipv6_interface(commandline_t *comline, int num, struct sockaddr *sa)
{
	struct sockaddr_in6 mcast_sin;
	struct sockaddr_in6 local_sin;
	struct ipv6_mreq mreq;
	int mcast_sock;
	int local_sock;
	int param;

	memset(&mcast_sin, 0, sizeof(mcast_sin));
	mcast_sin.sin6_family = AF_INET6;
	mcast_sin.sin6_port = htons(comline->port);

	memset(&local_sin, 0, sizeof(local_sin));
	memcpy(&local_sin, sa, sizeof(local_sin));
	local_sin.sin6_family = AF_INET6;
	local_sin.sin6_port = htons(comline->port);

	if (!comline->multicast_names[num])
		die("No multicast address for IPv6 node %s\n", comline->nodenames[num]);

	get_mcast_address(comline->multicast_names[num], AF_INET6, (struct sockaddr *)&mcast_sin);
	mcast_sin.sin6_port = htons(comline->port);

	mcast_sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	if (mcast_sock < 0)
		die("Can't open multicast socket: %s", strerror(errno));

	if (bind(mcast_sock, (struct sockaddr *)&mcast_sin, sizeof(mcast_sin)))
		die("Cannot bind multicast address: %s", strerror(errno));

	/* Join the multicast group */

	memcpy(&mreq.ipv6mr_multiaddr, &mcast_sin.sin6_addr, sizeof(mcast_sin.sin6_addr));
	mreq.ipv6mr_interface = if_nametoindex(comline->interfaces[num]);
	if (setsockopt(mcast_sock, SOL_IPV6, IPV6_ADD_MEMBERSHIP, (void *)&mreq, sizeof(mreq)))
		die("Unable to join multicast group %s: %s\n", comline->multicast_names[num], strerror(errno));

	param = 0; /* Disable local copies of multicast messages */
	setsockopt(mcast_sock, SOL_IPV6, IPV6_MULTICAST_LOOP, (void *)&param, sizeof(int));
	/* Failure of this is tolerable */


	/* Local socket */
	local_sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	if (local_sock < 0)
		die("Can't open local socket : %s", strerror(errno));

	if (bind(local_sock, (struct sockaddr *)&local_sin, sizeof(local_sin)))
		die("Cannot bind local address: %s", strerror(errno));

	set_priority(mcast_sock);
	set_priority(local_sock);

	add_argv_sock(num+1, local_sock, mcast_sock);

	return 0;
}


static int setup_interface(commandline_t *comline, int num)
{
	static int last_af = 0;
	int ret;
	struct addrinfo *ainfo;
	struct addrinfo ahints;

	memset(&ahints, 0, sizeof(ahints));
	ahints.ai_socktype = SOCK_DGRAM;
	ahints.ai_protocol = IPPROTO_UDP;
	/* Lookup the nodename address */
	ret = getaddrinfo(comline->nodenames[num], NULL,
			  &ahints,
			  &ainfo);

	if (ret)
		die("can't resolve node name %s: %s\n", comline->nodenames[num], gai_strerror(ret));

	if (ainfo->ai_next)
		die("node name %s is ambiguous\n", comline->nodenames[num]);

	/* All interfaces should be the same address family */
	if (last_af && (last_af != ainfo->ai_addr->sa_family))
		die("All IP addresses must have the same address family");

	last_af = ainfo->ai_addr->sa_family;

	if (last_af == AF_INET)
		ret = setup_ipv4_interface(comline, num, ainfo->ai_addr);
	else
		ret = setup_ipv6_interface(comline, num, ainfo->ai_addr);

	freeaddrinfo(ainfo);

	return ret;
}

int join(commandline_t *comline)
{
	cman_join_info_t join_info;
	int error, i;
	cman_handle_t h;
	char nodename[256];
	int argc;

	/*
	 * If we can talk to cman then we're already joined (or joining);
	 */
	h = cman_admin_init(NULL);
	if (h)
		die("Node is already active");

	/*
	 * Setup the interface/multicast
	 */
	argv[0] = "cmand";
	for (i = 0; i<comline->num_nodenames; i++)
	{
		error = setup_interface(comline, i);
		if (error)
			die("Unable to setup network interface(s)");
	}
	argc = comline->num_nodenames;

	if (comline->verbose)
		argv[++argc] = "-d";

	/* Terminate args */
	argv[++argc] = NULL;

	/* Fork/exec cman */
	switch (fork())
	{
	case -1:
		die("fork cman daemon failed: %s", strerror(errno));

	case 0: // child
		setsid();
		execve(SBINDIR "/cmand", argv, NULL);
		die("execve of " SBINDIR "/cmand failed: %s", strerror(errno));
		break;

	default: //parent
		break;

	}

	/* Give the daemon a chance to start up */
	i = 0;
	do {
		sleep(1);
		h = cman_admin_init(NULL);
		if (!h && comline->verbose) {
			fprintf(stderr, "waiting for cman to start\n");
		}
	} while (!h && ++i < 10);

	if (!h)
		die("cman daemon didn't start");


	/* Set the node name */
	strcpy(nodename, comline->nodenames[0]);

	if (comline->override_nodename)
		error = cman_set_nodename(h, comline->override_nodename);
	else
		error = cman_set_nodename(h, nodename);
	if (error)
		die("Unable to set cluster node name: %s", cman_error(errno));

	/* Optional, set the node ID */
	if (comline->nodeid) {
		error = cman_set_nodeid(h, comline->nodeid);
		if (error)
			die("Unable to set cluster nodeid: %s", cman_error(errno));
	}

	/*
	 * Join cluster
	 */
	join_info.ji_votes = comline->votes;
	join_info.ji_expected_votes = comline->expected_votes;
	strcpy(join_info.ji_cluster_name, comline->clustername);
	join_info.ji_two_node = comline->two_node;
	join_info.ji_config_version = comline->config_version;

	if (cman_join_cluster(h, &join_info))
	{
		die("error joining cluster: %s", cman_error(errno));
	}

	cman_finish(h);
	return 0;
}
