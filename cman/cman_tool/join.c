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

/* Size of buffer for gethostbyname2_r */
#define HE_BUFSIZE 512

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

static void set_priority(int sock)
{
    int prio = 3;

    if (setsockopt(sock, SOL_SOCKET, SO_PRIORITY, &prio, sizeof(int)))
	perror("Error setting socket priority");
}

static int setup_ipv4_interface(commandline_t *comline, int num, struct hostent *he)
{
    struct hostent realbhe;
    struct hostent *bhe = NULL;
    struct sockaddr_in mcast_sin;
    struct sockaddr_in local_sin;
    struct cl_passed_sock sock_info;
    char he_buffer[HE_BUFSIZE]; /* scratch area for gethostbyname2_r */
    int mcast_sock;
    int local_sock;
    int ret;
    int he_errno;
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
	ret = gethostbyname2_r(comline->multicast_names[num], AF_INET,
			       &realbhe, he_buffer, sizeof(he_buffer), &bhe,
			       &he_errno);
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
	int one=1;
	memcpy(&mcast_sin.sin_addr, &bcast, sizeof(struct in_addr));
	if (setsockopt(mcast_sock, SOL_SOCKET, SO_BROADCAST, (void *)&one, sizeof(int)))
	    die("Can't enable broadcast: %s", strerror(errno));
    }
    if (bind(mcast_sock, (struct sockaddr *)&mcast_sin, sizeof(mcast_sin)))
	die("Cannot bind multicast address: %s", strerror(errno));

    /* Join the multicast group */
    if (bhe) {
	struct ip_mreq mreq;
	char mcast_opt;

	memcpy(&mreq.imr_multiaddr, bhe->h_addr, bhe->h_length);
	memcpy(&mreq.imr_interface, he->h_addr, he->h_length);
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

    sock_info.number = num + 1;
    sock_info.multicast = 1;

    set_priority(mcast_sock);
    set_priority(local_sock);

    /* Pass the multicast socket to kernel space */
    sock_info.fd = mcast_sock;
    if (ioctl(cluster_sock, SIOCCLUSTER_PASS_SOCKET, (void *)&sock_info))
        die("passing multicast socket to cluster kernel module");

    /* Pass the recv socket to kernel space */
    sock_info.fd = local_sock;
    sock_info.multicast = 0;
    if (ioctl(cluster_sock, SIOCCLUSTER_PASS_SOCKET, (void *)&sock_info))
        die("passing unicast receive socket to cluster kernel module");

    /* These are now owned by the kernel */
    close(local_sock);
    close(mcast_sock);

    return 0;
}


static int setup_ipv6_interface(commandline_t *comline, int num, struct hostent *he)
{
    struct hostent realbhe;
    struct hostent *bhe = NULL;
    struct sockaddr_in6 mcast_sin;
    struct sockaddr_in6 local_sin;
    struct ipv6_mreq mreq;
    char he_buffer[HE_BUFSIZE]; /* scratch area for gethostbyname2_r */
    int ret;
    int he_errno;
    int mcast_sock;
    int local_sock;
    struct cl_passed_sock sock_info;
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

    ret = gethostbyname2_r(comline->multicast_names[num], AF_INET6,
			   &realbhe, he_buffer, sizeof(he_buffer), &bhe,
			   &he_errno);

    if (!bhe)
	die("Can't resolve multicast address %s\n", comline->multicast_names[num]);

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

    sock_info.number = num + 1;

    set_priority(mcast_sock);
    set_priority(local_sock);

    /* Pass the multicast socket to kernel space */
    sock_info.fd = mcast_sock;
    sock_info.multicast = 1;
    if (ioctl(cluster_sock, SIOCCLUSTER_PASS_SOCKET, (void *)&sock_info))
        die("passing multicast socket to cluster kernel module");

    /* Pass the recv socket to kernel space */
    sock_info.fd = local_sock;
    sock_info.multicast = 0;
    if (ioctl(cluster_sock, SIOCCLUSTER_PASS_SOCKET, (void *)&sock_info))
        die("passing unicast receive socket to cluster kernel module");

    /* These are now owned by the kernel */
    close(local_sock);
    close(mcast_sock);
    return 0;
}


static int setup_interface(commandline_t *comline, int num)
{
    struct hostent realhe;
    struct hostent *he;
    static int last_af = 0;
    char he_buffer[HE_BUFSIZE]; /* scratch area for gethostbyname2_r */
    int ret;
    int he_errno;
    int af;

    /* Lookup the nodename address */
    af = AF_INET6;
    ret = gethostbyname2_r(comline->nodenames[num], af,
			   &realhe, he_buffer, sizeof(he_buffer), &he,
			   &he_errno);
    if (!he) {
	af = AF_INET;
	ret = gethostbyname2_r(comline->nodenames[num], af,
			       &realhe, he_buffer, sizeof(he_buffer), &he,
			       &he_errno);
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
    char nodename[256];

    /*
     * Create the cluster master socket
     */
    cluster_sock = socket(AF_CLUSTER, SOCK_DGRAM, CLPROTO_MASTER);
    if (cluster_sock == -1)
    {
	int e = errno;
        perror("can't open cluster socket");

	if (e == EAFNOSUPPORT)
	    die("The cman kernel module may not be loaded");
	exit(EXIT_FAILURE);
    }

    /*
     * If the cluster is active then the interfaces are already set up
     * and this join should just add to the join_count.
     */
    error = ioctl(cluster_sock, SIOCCLUSTER_ISACTIVE);
    if (error == -1)
	die("Can't determine cluster state");

    if (error)
        die("Node is already active");

    /* Set the node name - without domain part */
    strcpy(nodename, comline->nodenames[0]);

    error = ioctl(cluster_sock, SIOCCLUSTER_SET_NODENAME, nodename);
    if (error)
	die("Unable to set cluster node name");

    /* Optional, set the node ID */
    if (comline->nodeid) {
	error = ioctl(cluster_sock, SIOCCLUSTER_SET_NODEID,
		      comline->nodeid);
	if (error)
	    die("Unable to set cluster nodeid");
    }

    /*
     * Setup the interface/multicast
     */
    for (i = 0; i<comline->num_nodenames; i++)
    {
	error = setup_interface(comline, i);
	if (error)
	    die("Unable to setup network interface(s)");
    }

    /*
     * Join cluster
     */
    join_info.votes = comline->votes;
    join_info.expected_votes = comline->expected_votes;
    strcpy(join_info.cluster_name, comline->clustername);
    join_info.two_node = comline->two_node;
    join_info.config_version = comline->config_version;

    if (ioctl(cluster_sock, SIOCCLUSTER_JOIN_CLUSTER, &join_info))
    {
        die("error joining cluster");
    }

    close(cluster_sock);
    return 0;
}
