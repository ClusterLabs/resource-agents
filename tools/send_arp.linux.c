/*
 * arping.c
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 */

#include <stdlib.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <linux/sockios.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <net/if_arp.h>
#include <sys/uio.h>

#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static void usage(void) __attribute__((noreturn));

static int quit_on_reply;
static char *device;
static int ifindex;
static char *source;
static struct in_addr src, dst;
static char *target;
static int dad = 0, unsolicited = 0, advert = 0;
static int quiet = 0;
static int count = -1;
static int timeout = 0;
static int unicasting = 0;
static int s = 0;
static int broadcast_only = 0;

static struct sockaddr_ll me;
static struct sockaddr_ll he;

static struct timeval start, last;

static int sent, brd_sent;
static int received, brd_recv, req_recv;

#define MS_TDIFF(tv1,tv2) ( ((tv1).tv_sec-(tv2).tv_sec)*1000 + \
			   ((tv1).tv_usec-(tv2).tv_usec)/1000 )

static void print_hex(unsigned char *p, int len);
static int recv_pack(unsigned char *buf, int len, struct sockaddr_ll *FROM);
static void set_signal(int signo, void (*handler)(void));
static int send_pack(int s, struct in_addr src, struct in_addr dst,
	      struct sockaddr_ll *ME, struct sockaddr_ll *HE);
static void finish(void);
static void catcher(void);

void usage(void)
{
	fprintf(stderr,
		"Usage: arping [-fqbDUAV] [-c count] [-w timeout] [-I device] [-s source] destination\n"
		"  -f : quit on first reply\n"
		"  -q : be quiet\n"
		"  -b : keep broadcasting, don't go unicast\n"
		"  -D : duplicate address detection mode\n"
		"  -U : Unsolicited ARP mode, update your neighbours\n"
		"  -A : ARP answer mode, update your neighbours\n"
		"  -V : print version and exit\n"
		"  -c count : how many packets to send\n"
		"  -w timeout : how long to wait for a reply\n"
		"  -I device : which ethernet device to use (eth0)\n"
		"  -s source : source ip address\n"
		"  destination : ask for what ip address\n"
		);
	exit(2);
}

void set_signal(int signo, void (*handler)(void))
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = (void (*)(int))handler;
	sa.sa_flags = SA_RESTART;
	sigaction(signo, &sa, NULL);
}

int send_pack(int s, struct in_addr src, struct in_addr dst,
	      struct sockaddr_ll *ME, struct sockaddr_ll *HE)
{
	int err;
	struct timeval now;
	unsigned char buf[256];
	struct arphdr *ah = (struct arphdr*)buf;
	unsigned char *p = (unsigned char *)(ah+1);

	ah->ar_hrd = htons(ME->sll_hatype);
	if (ah->ar_hrd == htons(ARPHRD_FDDI))
		ah->ar_hrd = htons(ARPHRD_ETHER);
	ah->ar_pro = htons(ETH_P_IP);
	ah->ar_hln = ME->sll_halen;
	ah->ar_pln = 4;
	ah->ar_op  = advert ? htons(ARPOP_REPLY) : htons(ARPOP_REQUEST);

	memcpy(p, &ME->sll_addr, ah->ar_hln);
	p+=ME->sll_halen;

	memcpy(p, &src, 4);
	p+=4;

	if (advert)
		memcpy(p, &ME->sll_addr, ah->ar_hln);
	else
		memcpy(p, &HE->sll_addr, ah->ar_hln);
	p+=ah->ar_hln;

	memcpy(p, &dst, 4);
	p+=4;

	gettimeofday(&now, NULL);
	err = sendto(s, buf, p-buf, 0, (struct sockaddr*)HE, sizeof(*HE));
	if (err == p-buf) {
		last = now;
		sent++;
		if (!unicasting)
			brd_sent++;
	}
	return err;
}

void finish(void)
{
	if (!quiet) {
		printf("Sent %d probes (%d broadcast(s))\n", sent, brd_sent);
		printf("Received %d response(s)", received);
		if (brd_recv || req_recv) {
			printf(" (");
			if (req_recv)
				printf("%d request(s)", req_recv);
			if (brd_recv)
				printf("%s%d broadcast(s)",
				       req_recv ? ", " : "",
				       brd_recv);
			printf(")");
		}
		printf("\n");
		fflush(stdout);
	}

	if (dad) {
	    fflush(stdout);
	    exit(!!received);
	}
	
	if (unsolicited)
		exit(0);

	fflush(stdout);
	exit(!received);
}

void catcher(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);

	if (start.tv_sec==0)
		start = tv;

	if (count-- == 0 || (timeout && MS_TDIFF(tv,start) > timeout*1000 + 500))
		finish();

	if (last.tv_sec==0 || MS_TDIFF(tv,last) > 500) {
		send_pack(s, src, dst, &me, &he);
		if (count == 0 && unsolicited)
			finish();
	}
	alarm(1);
}

void print_hex(unsigned char *p, int len)
{
	int i;
	for (i=0; i<len; i++) {
		printf("%02X", p[i]);
		if (i != len-1)
			printf(":");
	}
}

int recv_pack(unsigned char *buf, int len, struct sockaddr_ll *FROM)
{
	struct timeval tv;
	struct arphdr *ah = (struct arphdr*)buf;
	unsigned char *p = (unsigned char *)(ah+1);
	struct in_addr src_ip, dst_ip;

	gettimeofday(&tv, NULL);

	/* Filter out wild packets */
	if (FROM->sll_pkttype != PACKET_HOST &&
	    FROM->sll_pkttype != PACKET_BROADCAST &&
	    FROM->sll_pkttype != PACKET_MULTICAST)
		return 0;

	/* Only these types are recognised */
	if (ah->ar_op != htons(ARPOP_REQUEST) &&
	    ah->ar_op != htons(ARPOP_REPLY))
		return 0;

	/* ARPHRD check and this darned FDDI hack here :-( */
	if (ah->ar_hrd != htons(FROM->sll_hatype) &&
	    (FROM->sll_hatype != ARPHRD_FDDI || ah->ar_hrd != htons(ARPHRD_ETHER)))
		return 0;

	/* Protocol must be IP. */
	if (ah->ar_pro != htons(ETH_P_IP))
		return 0;
	if (ah->ar_pln != 4)
		return 0;
	if (ah->ar_hln != me.sll_halen)
		return 0;
	if (len < sizeof(*ah) + 2*(4 + ah->ar_hln))
		return 0;
	memcpy(&src_ip, p+ah->ar_hln, 4);
	memcpy(&dst_ip, p+ah->ar_hln+4+ah->ar_hln, 4);
	if (!dad) {
		if (src_ip.s_addr != dst.s_addr)
			return 0;
		if (src.s_addr != dst_ip.s_addr)
			return 0;
		if (memcmp(p+ah->ar_hln+4, &me.sll_addr, ah->ar_hln))
			return 0;
	} else {
		/* DAD packet was:
		   src_ip = 0 (or some src)
		   src_hw = ME
		   dst_ip = tested address
		   dst_hw = <unspec>

		   We fail, if receive request/reply with:
		   src_ip = tested_address
		   src_hw != ME
		   if src_ip in request was not zero, check
		   also that it matches to dst_ip, otherwise
		   dst_ip/dst_hw do not matter.
		 */
		if (src_ip.s_addr != dst.s_addr)
			return 0;
		if (memcmp(p, &me.sll_addr, me.sll_halen) == 0)
			return 0;
		if (src.s_addr && src.s_addr != dst_ip.s_addr)
			return 0;
	}
	if (!quiet) {
		int s_printed = 0;
		printf("%s ", FROM->sll_pkttype==PACKET_HOST ? "Unicast" : "Broadcast");
		printf("%s from ", ah->ar_op == htons(ARPOP_REPLY) ? "reply" : "request");
		printf("%s [", inet_ntoa(src_ip));
		print_hex(p, ah->ar_hln);
		printf("] ");
		if (dst_ip.s_addr != src.s_addr) {
			printf("for %s ", inet_ntoa(dst_ip));
			s_printed = 1;
		}
		if (memcmp(p+ah->ar_hln+4, me.sll_addr, ah->ar_hln)) {
			if (!s_printed)
				printf("for ");
			printf("[");
			print_hex(p+ah->ar_hln+4, ah->ar_hln);
			printf("]");
		}
		if (last.tv_sec) {
			long usecs = (tv.tv_sec-last.tv_sec) * 1000000 +
				tv.tv_usec-last.tv_usec;
			long msecs = (usecs+500)/1000;
			usecs -= msecs*1000 - 500;
			printf(" %ld.%03ldms\n", msecs, usecs);
		} else {
			printf(" UNSOLICITED?\n");
		}
		fflush(stdout);
	}
	received++;
	if (FROM->sll_pkttype != PACKET_HOST)
		brd_recv++;
	if (ah->ar_op == htons(ARPOP_REQUEST))
		req_recv++;
	if (quit_on_reply)
		finish();
	if(!broadcast_only) {
		memcpy(he.sll_addr, p, me.sll_halen);
		unicasting=1;
	}
	return 1;
}

#include <signal.h>

static void byebye(int nsig)
{
    /* Avoid an "error exit" log message if we're killed */
    nsig = 0;
    exit(nsig);
}

int
main(int argc, char **argv)
{
	int socket_errno;
	int ch;
	uid_t uid = getuid();
	int hb_mode = 0;

	signal(SIGTERM, byebye);
	signal(SIGPIPE, byebye);
	
	device = strdup("eth0");
	
	s = socket(PF_PACKET, SOCK_DGRAM, 0);
	socket_errno = errno;

	if (setuid(uid)) {
		perror("arping: setuid");
		exit(-1);
	}

	while ((ch = getopt(argc, argv, "h?bfDUAqc:w:s:I:Vr:i:p:")) != EOF) {
		switch(ch) {
		case 'b':
			broadcast_only=1;
			break;
		case 'D':
			dad++;
			quit_on_reply=1;
			break;
		case 'U':
			unsolicited++;
			break;
		case 'A':
			advert++;
			unsolicited++;
			break;
		case 'q':
			quiet++;
			break;
		case 'r': /* send_arp.libnet compatibility option */
			hb_mode = 1;
			/* fall-through */
		case 'c':
			count = atoi(optarg);
			break;
		case 'w':
			timeout = atoi(optarg);
			break;
		case 'I':
			device = optarg;
			break;
		case 'f':
			quit_on_reply=1;
			break;
		case 's':
			source = optarg;
			break;
		case 'V':
			printf("send_arp utility\n");
			exit(0);
		case 'p':
		case 'i':
		    hb_mode = 1;
		    /* send_arp.libnet compatibility options, ignore */
		    break;
		case 'h':
		case '?':
		default:
			usage();
		}
	}

	if(hb_mode) {
	    /* send_arp.libnet compatibility mode */
	    if (argc - optind != 5) {
		usage();
		return 1;
	    }
	    /*
	     *	argv[optind+1] DEVICE		dc0,eth0:0,hme0:0,
	     *	argv[optind+2] IP		192.168.195.186
	     *	argv[optind+3] MAC ADDR		00a0cc34a878
	     *	argv[optind+4] BROADCAST	192.168.195.186
	     *	argv[optind+5] NETMASK		ffffffffffff
	     */

	    unsolicited = 1;
	    device = argv[optind];
	    target = argv[optind+1];

	} else {
	    argc -= optind;
	    argv += optind;
	    if (argc != 1)
		usage();

	    target = *argv;
	}
	
	if (device == NULL) {
		fprintf(stderr, "arping: device (option -I) is required\n");
		usage();
	}

	if (s < 0) {
		errno = socket_errno;
		perror("arping: socket");
		exit(2);
	}

	if (1) {
		struct ifreq ifr;
		memset(&ifr, 0, sizeof(ifr));
		strncpy(ifr.ifr_name, device, IFNAMSIZ-1);
		if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
			fprintf(stderr, "arping: unknown iface %s\n", device);
			exit(2);
		}
		ifindex = ifr.ifr_ifindex;

		if (ioctl(s, SIOCGIFFLAGS, (char*)&ifr)) {
			perror("ioctl(SIOCGIFFLAGS)");
			exit(2);
		}
		if (!(ifr.ifr_flags&IFF_UP)) {
			if (!quiet)
				printf("Interface \"%s\" is down\n", device);
			exit(2);
		}
		if (ifr.ifr_flags&(IFF_NOARP|IFF_LOOPBACK)) {
			if (!quiet)
				printf("Interface \"%s\" is not ARPable\n", device);
			exit(dad?0:2);
		}
	}

	if (inet_aton(target, &dst) != 1) {
		struct hostent *hp;
		hp = gethostbyname2(target, AF_INET);
		if (!hp) {
			fprintf(stderr, "arping: unknown host %s\n", target);
			exit(2);
		}
		memcpy(&dst, hp->h_addr, 4);
	}

	if (source && inet_aton(source, &src) != 1) {
		fprintf(stderr, "arping: invalid source %s\n", source);
		exit(2);
	}

	if (!dad && unsolicited && src.s_addr == 0)
		src = dst;

	if (!dad || src.s_addr) {
		struct sockaddr_in saddr;
		int probe_fd = socket(AF_INET, SOCK_DGRAM, 0);

		if (probe_fd < 0) {
			perror("socket");
			exit(2);
		}
		if (device) {
			if (setsockopt(probe_fd, SOL_SOCKET, SO_BINDTODEVICE, device, strlen(device)+1) == -1)
				perror("WARNING: interface is ignored");
		}
		memset(&saddr, 0, sizeof(saddr));
		saddr.sin_family = AF_INET;
		if (src.s_addr) {
			saddr.sin_addr = src;
			if (bind(probe_fd, (struct sockaddr*)&saddr, sizeof(saddr)) == -1) {
				perror("bind");
				exit(2);
			}
		} else if (!dad) {
			int on = 1;
			socklen_t alen = sizeof(saddr);

			saddr.sin_port = htons(1025);
			saddr.sin_addr = dst;

			if (setsockopt(probe_fd, SOL_SOCKET, SO_DONTROUTE, (char*)&on, sizeof(on)) == -1)
				perror("WARNING: setsockopt(SO_DONTROUTE)");
			if (connect(probe_fd, (struct sockaddr*)&saddr, sizeof(saddr)) == -1) {
				perror("connect");
				exit(2);
			}
			if (getsockname(probe_fd, (struct sockaddr*)&saddr, &alen) == -1) {
				perror("getsockname");
				exit(2);
			}
			src = saddr.sin_addr;
		}
		close(probe_fd);
	};

	me.sll_family = AF_PACKET;
	me.sll_ifindex = ifindex;
	me.sll_protocol = htons(ETH_P_ARP);
	if (bind(s, (struct sockaddr*)&me, sizeof(me)) == -1) {
		perror("bind");
		exit(2);
	}

	if (1) {
		socklen_t alen = sizeof(me);
		if (getsockname(s, (struct sockaddr*)&me, &alen) == -1) {
			perror("getsockname");
			exit(2);
		}
	}
	if (me.sll_halen == 0) {
		if (!quiet)
			printf("Interface \"%s\" is not ARPable (no ll address)\n", device);
		exit(dad?0:2);
	}

	he = me;
	memset(he.sll_addr, -1, he.sll_halen);

	if (!quiet) {
		printf("ARPING %s ", inet_ntoa(dst));
		printf("from %s %s\n",  inet_ntoa(src), device ? : "");
	}

	if (!src.s_addr && !dad) {
		fprintf(stderr, "arping: no source address in not-DAD mode\n");
		exit(2);
	}

	set_signal(SIGINT, finish);
	set_signal(SIGALRM, catcher);

	catcher();

	while(1) {
		sigset_t sset, osset;
		unsigned char packet[4096];
		struct sockaddr_ll from;
		socklen_t alen = sizeof(from);
		int cc;

		if ((cc = recvfrom(s, packet, sizeof(packet), 0,
				   (struct sockaddr *)&from, &alen)) < 0) {
			perror("arping: recvfrom");
			continue;
		}
		sigemptyset(&sset);
		sigaddset(&sset, SIGALRM);
		sigaddset(&sset, SIGINT);
		sigprocmask(SIG_BLOCK, &sset, &osset);
		recv_pack(packet, cc, &from);
		sigprocmask(SIG_SETMASK, &osset, NULL);
	}
}


