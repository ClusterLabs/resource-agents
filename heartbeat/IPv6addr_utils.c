
/*
 * This program manages IPv6 address with OCF Resource Agent standard.
 *
 * Author: Huang Zhen <zhenh@cn.ibm.com>
 * Copyright (c) 2004 International Business Machines
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <IPv6addr.h>

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h> /* for inet_pton */
#include <net/if.h> /* for if_nametoindex */
#include <sys/ioctl.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

/* Send an unsolicited advertisement packet
 * Please refer to rfc4861 / rfc3542
 */
int
send_ua(struct in6_addr* src_ip, char* if_name)
{
	int status = -1;
	int fd;

	int ifindex;
	int hop;
	struct ifreq ifr;
	u_int8_t *payload = NULL;
	int    payload_size;
	struct nd_neighbor_advert *na;
	struct nd_opt_hdr *opt;
	struct sockaddr_in6 src_sin6;
	struct sockaddr_in6 dst_sin6;

	if ((fd = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6)) == -1) {
		printf("ERROR: socket(IPPROTO_ICMPV6) failed: %s",
		       strerror(errno));
		return status;
	}
	/* set the outgoing interface */
	ifindex = if_nametoindex(if_name);
	if (setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_IF,
		       &ifindex, sizeof(ifindex)) < 0) {
		printf("ERROR: setsockopt(IPV6_MULTICAST_IF) failed: %s",
		       strerror(errno));
		goto err;
	}
	/* set the hop limit */
	hop = 255; /* 255 is required. see rfc4861 7.1.2 */
	if (setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
		       &hop, sizeof(hop)) < 0) {
		printf("ERROR: setsockopt(IPV6_MULTICAST_HOPS) failed: %s",
		       strerror(errno));
		goto err;
	}

	/* set the source address */
	memset(&src_sin6, 0, sizeof(src_sin6));
	src_sin6.sin6_family = AF_INET6;
	src_sin6.sin6_addr = *src_ip;
	src_sin6.sin6_port = 0;
	if (IN6_IS_ADDR_LINKLOCAL(&src_sin6.sin6_addr) ||
	    IN6_IS_ADDR_MC_LINKLOCAL(&src_sin6.sin6_addr)) {
		src_sin6.sin6_scope_id = ifindex;
	}

	if (bind(fd, (struct sockaddr *)&src_sin6, sizeof(src_sin6)) < 0) {
		printf("ERROR: bind() failed: %s", strerror(errno));
		goto err;
	}


	/* get the hardware address */
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, if_name, sizeof(ifr.ifr_name) - 1);
	if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) {
		printf("ERROR: ioctl(SIOCGIFHWADDR) failed: %s", strerror(errno));
		goto err;
	}

	/* build a neighbor advertisement message */
	payload_size = sizeof(struct nd_neighbor_advert)
			 + sizeof(struct nd_opt_hdr) + HWADDR_LEN;
	payload = memalign(sysconf(_SC_PAGESIZE), payload_size);
	if (!payload) {
		printf("ERROR: malloc for payload failed");
		goto err;
	}
	memset(payload, 0, payload_size);

	/* Ugly typecast from ia64 hell! */
	na = (struct nd_neighbor_advert *)((void *)payload);
	na->nd_na_type = ND_NEIGHBOR_ADVERT;
	na->nd_na_code = 0;
	na->nd_na_cksum = 0; /* calculated by kernel */
	na->nd_na_flags_reserved = ND_NA_FLAG_OVERRIDE;
	na->nd_na_target = *src_ip;

	/* options field; set the target link-layer address */
	opt = (struct nd_opt_hdr *)(payload + sizeof(struct nd_neighbor_advert));
	opt->nd_opt_type = ND_OPT_TARGET_LINKADDR;
	opt->nd_opt_len = 1; /* The length of the option in units of 8 octets */
	memcpy(payload + sizeof(struct nd_neighbor_advert)
			+ sizeof(struct nd_opt_hdr),
	       &ifr.ifr_hwaddr.sa_data, HWADDR_LEN);

	/* sending an unsolicited neighbor advertisement to all */
	memset(&dst_sin6, 0, sizeof(dst_sin6));
	dst_sin6.sin6_family = AF_INET6;
	inet_pton(AF_INET6, BCAST_ADDR, &dst_sin6.sin6_addr); /* should not fail */

	if (sendto(fd, payload, payload_size, 0,
		   (struct sockaddr *)&dst_sin6, sizeof(dst_sin6))
	    != payload_size) {
		printf("ERROR: sendto(%s) failed: %s",
		       if_name, strerror(errno));
		goto err;
	}

	status = 0;

err:
	close(fd);
	free(payload);
	return status;
}
