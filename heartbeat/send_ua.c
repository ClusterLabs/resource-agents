
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

static void usage_send_ua(const char* self);
static void byebye(int nsig);

int
main(int argc, char* argv[])
{
	char*		ipv6addr;
	int		count = UA_REPEAT_COUNT;
	int		interval = 1000;	/* default 1000 msec */
	int		ch;
	int		i;
	char*		cp;
	char*		prov_ifname = NULL;
	struct in6_addr	addr6;

	/* Check binary name */
	if (argc < 4) {
		usage_send_ua(argv[0]);
		return OCF_ERR_ARGS;
	}
	while ((ch = getopt(argc, argv, "h?c:i:")) != EOF) {
		switch(ch) {
		case 'c': /* count option */
			count = atoi(optarg);
		    break;
		case 'i': /* interval option */
			interval = atoi(optarg);
		    break;
		case 'h':
		case '?':
		default:
			usage_send_ua(argv[0]);
			return OCF_ERR_ARGS;
		}
	}

	/* set termination signal */
	siginterrupt(SIGTERM, 1);
	signal(SIGTERM, byebye);

	ipv6addr = argv[optind];

	if (ipv6addr == NULL) {
		printf("ERROR: Please set OCF_RESKEY_ipv6addr to the IPv6 address you want to manage.");
		usage_send_ua(argv[0]);
		return OCF_ERR_ARGS;
	}

	/* legacy option */
	if ((cp = strchr(ipv6addr, '/'))) {
		*cp=0;
	}

	prov_ifname = argv[optind+2];

	if (inet_pton(AF_INET6, ipv6addr, &addr6) <= 0) {
		printf("ERROR: Invalid IPv6 address [%s]", ipv6addr);
		usage_send_ua(argv[0]);
		return OCF_ERR_ARGS;
	}

	/* Check whether this system supports IPv6 */
	if (access(IF_INET6, R_OK)) {
		printf("ERROR: No support for INET6 on this system.");
		return OCF_ERR_GENERIC;
	}

	/* Send unsolicited advertisement packet to neighbor */
	for (i = 0; i < count; i++) {
		send_ua(&addr6, prov_ifname);
		usleep(interval * 1000);
	}

	return OCF_SUCCESS;
}

static void usage_send_ua(const char* self)
{
	printf("usage: %s [-i[=Interval]] [-c[=Count]] [-h] IPv6-Address Prefix Interface\n",self);
	return;
}

/* Following code is copied from send_arp.c, linux-HA project. */
void
byebye(int nsig)
{
	(void)nsig;
	/* Avoid an "error exit" log message if we're killed */
	exit(0);
}

