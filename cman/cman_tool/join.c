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
#include "libcman.h"
#include "cman_tool.h"

/* Size of buffer for gethostbyname2_r */
#define HE_BUFSIZE 2048

static char *argv[128];

/* Lookup the IPv4 broadcast address for a given local address */
static uint32_t lookup_bcast(uint32_t localaddr, char *ifname)
{
    struct ifreq ifr;
    uint32_t addr, brdaddr;
    int iindex;
    int sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in *saddr = (struct sockaddr_in *)&ifr.ifr_ifru.ifru_addr;

    ifname[0] = '\0';

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
		strcpy(ifname, ifr.ifr_name);
		return brdaddr;
	    }
	}
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

static int setup_ipv4_interface(commandline_t *comline, int num, struct hostent *he)
{
    struct hostent realbhe;
    struct hostent *bhe = NULL;
    struct sockaddr_in mcast_sin;
    struct sockaddr_in local_sin;
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
	char ifname[256];

	memcpy(&ipaddr, he->h_addr, sizeof(uint32_t));
	bcast = lookup_bcast(ipaddr, ifname);
	if (!bcast) {
	    fprintf(stderr, "%s: Can't find broadcast address for node %s\n", prog_name, comline->nodenames[num]);
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
	ret = gethostbyname2_r(comline->multicast_names[num], AF_INET,
			       &realbhe, he_buffer, sizeof(he_buffer), &bhe,
			       &he_errno);
	if (!bhe)
	    die("Can't resolve multicast address %s: %s\n", comline->multicast_names[num], hstrerror(he_errno));

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

    set_priority(mcast_sock);
    set_priority(local_sock);

    add_argv_sock(num+1, local_sock, mcast_sock);

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
	die("Can't resolve multicast address %s: %s\n", comline->multicast_names[num], hstrerror(he_errno));

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

    set_priority(mcast_sock);
    set_priority(local_sock);

    add_argv_sock(num+1, local_sock, mcast_sock);

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
	die("can't resolve node name %s: %s\n", comline->nodenames[num], hstrerror(errno));

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
	    if (!h) {
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
