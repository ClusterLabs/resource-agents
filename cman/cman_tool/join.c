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

#include <net/route.h>
#include "cnxman-socket.h"
#include "cman_tool.h"

static int cluster_sock;

/* Lookup the IPv4 broadcast address for a given local address */
static uint32_t lookup_bcast(uint32_t localaddr)
{
    struct ifreq ifr;
    uint32_t addr, brdaddr;
    int iindex;
    int sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in *saddr = (struct sockaddr_in *)&ifr.ifr_ifru.ifru_addr;

    for (iindex = 0; iindex < 16; iindex++) {
	ifr.ifr_ifindex = iindex;
	if (ioctl(sock, SIOCGIFNAME, &ifr) == 0) {
	    ifr.ifr_ifindex = iindex;

	    ioctl(sock, SIOCGIFADDR, &ifr);
	    addr = saddr->sin_addr.s_addr;

	    if (addr == localaddr) {
		ioctl(sock, SIOCGIFBRDADDR, &ifr);
		brdaddr = saddr->sin_addr.s_addr;

		close(sock);
		return brdaddr;
	    }
	}
    }

    /* Didn't find it */
    close(sock);
    return 0;
}

static int setup_ipv4_interface(commandline_t *comline, int num, struct hostent *he)
{
    struct hostent *bhe = NULL;
    struct sockaddr_in mcast_sin;
    struct sockaddr_in local_sin;
    struct cl_multicast_sock mcast_info;
    int mcast_sock;
    int local_sock;
    uint32_t bcast;

    memset(&mcast_sin, 0, sizeof(mcast_sin));
    mcast_sin.sin_family = AF_INET;
    mcast_sin.sin_port = htons(comline->port);

    memset(&local_sin, 0, sizeof(local_sin));
    local_sin.sin_family = AF_INET;
    local_sin.sin_port = htons(comline->port);
    memcpy(&local_sin.sin_addr, he->h_addr, he->h_length);

    if (comline->verbose)
	printf("setup up interface for address: %s\n", comline->nodenames[num]);

    if (!comline->multicast_names[num]) {
	uint32_t ipaddr;

	memcpy(&ipaddr, he->h_addr, sizeof(uint32_t));
	bcast = lookup_bcast(ipaddr);
	if (!bcast)
	    die("Can't find broadcast address for node %s\n", comline->nodenames[num]);

	if (comline->verbose) {
	    printf("Broadcast address for %x is %x\n", ipaddr, bcast);
	}
    }
    else {
	uint32_t addr;
	bhe = gethostbyname2(comline->multicast_names[num], AF_INET);
	if (!bhe)
	    die("Can't resolve multicast address %s\n", comline->multicast_names[num]);

	if (bhe->h_addr_list[1])
	    die("multicast address %s is ambiguous\n", comline->multicast_names[num]);

	/* Check it really is a multicast address */
	memcpy(&addr, bhe->h_addr_list[0], sizeof(uint32_t));
	if ((ntohl(addr) & 0xe0000000) != 0xe0000000)
		die("%s is not an IPv4 multicast address\n", comline->multicast_names[num]);
    }

    mcast_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (mcast_sock < 0)
	die("Can't open multicast socket: %s", strerror(errno));


    if (bhe) {
	/* Multicast */
	memcpy(&mcast_sin.sin_addr, bhe->h_addr, bhe->h_length);
    }
    else {
	/* Broadcast */
	int one;
	memcpy(&mcast_sin.sin_addr, &bcast, sizeof(struct in_addr));
	if (setsockopt(mcast_sock, SOL_SOCKET, SO_BROADCAST, (void *)&one, sizeof(int)))
	    die("Can't enable broadcast: %s", strerror(errno));
    }
    if (bind(mcast_sock, (struct sockaddr *)&mcast_sin, sizeof(mcast_sin)))
	die("Cannot bind multicast address: %s", strerror(errno));

    /* Join the multicast group */
    if (!bcast) {
	struct ip_mreq mreq;

	memcpy(&mreq.imr_multiaddr, bhe->h_addr, bhe->h_length);
	memcpy(&mreq.imr_interface, he->h_addr, he->h_length);
	if (setsockopt(mcast_sock, SOL_IP, IP_ADD_MEMBERSHIP, (void *)&mreq, sizeof(mreq)))
	    die("Unable to join multicast group %s: %s\n", comline->multicast_names[num], strerror(errno));
    }

    /* Local socket */
    local_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (local_sock < 0)
	die("Can't open local socket: %s", strerror(errno));

    if (bind(local_sock, (struct sockaddr *)&local_sin, sizeof(local_sin)))
	die("Cannot bind local address: %s", strerror(errno));

    mcast_info.number = num;

    /* Pass the multicast socket to kernel space */
    mcast_info.fd = mcast_sock;
    if (setsockopt(cluster_sock, CLPROTO_MASTER, CLU_SET_MULTICAST,
               (void *)&mcast_info, sizeof(mcast_info)))
        die("passing multicast socket to cluster kernel module");

    /* Pass the recv socket to kernel space */
    mcast_info.fd = local_sock;
    if (setsockopt(cluster_sock, CLPROTO_MASTER, CLU_SET_RCVONLY,
               (void *)&mcast_info, sizeof(mcast_info)))
        die("passing unicast receive socket to cluster kernel module");

    return 0;
}


static int setup_ipv6_interface(commandline_t *comline, int num, struct hostent *he)
{
    struct hostent *bhe;
    struct sockaddr_in6 mcast_sin;
    struct sockaddr_in6 local_sin;
    struct ipv6_mreq mreq;
    int mcast_sock;
    int local_sock;
    struct cl_multicast_sock mcast_info;
    int param;

    memset(&mcast_sin, 0, sizeof(mcast_sin));
    mcast_sin.sin6_family = AF_INET6;
    mcast_sin.sin6_port = htons(comline->port);

    memset(&local_sin, 0, sizeof(local_sin));
    local_sin.sin6_family = AF_INET6;
    local_sin.sin6_port = htons(comline->port);
    memcpy(&local_sin.sin6_addr, he->h_addr, he->h_length);

    if (!comline->multicast_names[num])
	die("No multicast address for IPv6 node %s\n", comline->nodenames[num]);

    bhe = gethostbyname2(comline->multicast_names[num], AF_INET6);

    mcast_sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (mcast_sock < 0)
	die("Can't open multicast socket: %s", strerror(errno));

    memcpy(&mcast_sin.sin6_addr, bhe->h_addr, bhe->h_length);
    if (bind(mcast_sock, (struct sockaddr *)&mcast_sin, sizeof(mcast_sin))) {
	die("Cannot bind multicast address: %s", strerror(errno));
    }

    /* Join the multicast group */

    memcpy(&mreq.ipv6mr_multiaddr, bhe->h_addr, bhe->h_length);
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

    mcast_info.number = num;

    /* Pass the multicast socket to kernel space */
    mcast_info.fd = mcast_sock;
    if (setsockopt(cluster_sock, CLPROTO_MASTER, CLU_SET_MULTICAST,
               (void *)&mcast_info, sizeof(mcast_info)))
        die("passing multicast socket to cluster kernel module");

    /* Pass the recv socket to kernel space */
    mcast_info.fd = local_sock;
    if (setsockopt(cluster_sock, CLPROTO_MASTER, CLU_SET_RCVONLY,
               (void *)&mcast_info, sizeof(mcast_info)))
        die("passing unicast receive socket to cluster kernel module");

    return 0;
}


static int setup_interface(commandline_t *comline, int num)
{
    struct hostent *he;
    static int last_af = 0;
    int af;

    /* Lookup the nodename address */
    af = AF_INET6;
    he = gethostbyname2(comline->nodenames[num], af);
    if (!he) {
	af = AF_INET;
	he = gethostbyname2(comline->nodenames[num], af);
    }
    if (!he)
	die("can't resolve node name %s\n", comline->nodenames[num]);

    if (he->h_addr_list[1])
	die("node name %s is ambiguous\n", comline->nodenames[num]);

    /* All interfaces should be the same address family */
    if (last_af && (last_af != af))
	die("All IP addresses must have the same address family");

    last_af = af;

    if (af == AF_INET)
	return setup_ipv4_interface(comline, num, he);
    else
	return setup_ipv6_interface(comline, num, he);
}

int join(commandline_t *comline)
{
    struct cl_join_cluster_info join_info;
    int error, i;

    /*
     * Create the cluster master socket
     */
    cluster_sock = socket(AF_CLUSTER, SOCK_DGRAM, CLPROTO_MASTER);
    if (cluster_sock == -1)
    {
        perror("can't open cluster socket");
	return -1;
    }


    /*
     * If the cluster is active then the interfaces are already set up
     * and this join should just add to the join_count.
     */
    error = ioctl(cluster_sock, SIOCCLUSTER_ISACTIVE);
    if (error == -1)
    {
	perror("Can't determine cluster state");
	goto fail;
    }
    if (error)
    {
        fprintf(stderr, "Node is already active\n");
        goto fail;
    }

    /*
     * Setup the interface/multicast
     */
    for (i = 0; i<comline->num_nodenames; i++)
    {
	error = setup_interface(comline, i);
	if (error)
	    goto fail;
    }

    /*
     * Join cluster
     */
    join_info.votes = comline->votes;
    join_info.expected_votes = comline->expected_votes;
    strcpy(join_info.cluster_name, comline->clustername);
    join_info.two_node = comline->two_node;
    join_info.config_version = comline->config_version;

    if (setsockopt(cluster_sock, CLPROTO_MASTER, CLU_JOIN_CLUSTER, &join_info,
                sizeof(join_info)))
    {
        perror("error joining cluster");
        goto fail;
    }

    close(cluster_sock);
    return 0;

 fail:
    close(cluster_sock);
    return -1;
}
