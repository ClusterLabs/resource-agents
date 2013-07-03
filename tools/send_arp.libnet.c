/* 
 * send_arp
 * 
 * This program sends out one ARP packet with source/target IP and Ethernet
 * hardware addresses suuplied by the user.  It uses the libnet libary from
 * Packet Factory (http://www.packetfactory.net/libnet/ ). It has been tested
 * on Linux, FreeBSD, and on Solaris.
 * 
 * This inspired by the sample application supplied by Packet Factory.

 * Matt Soffen

 * Copyright (C) 2001 Matt Soffen <matt@soffen.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/* Needs to be defined before any other includes, otherwise some system
 * headers do not behave as expected! Major black magic... */
#undef _GNU_SOURCE  /* in case it was defined on the command line */
#define _GNU_SOURCE

#include <config.h>
#include <sys/param.h>

#define USE_GNU
#if defined(ANSI_ONLY) && !defined(inline)
#	define inline	/* nothing */
#endif

#include <limits.h>
#include <libnet.h>
#include <libgen.h>
#include <clplumbing/timers.h>
#include <clplumbing/cl_signal.h>
#include <clplumbing/cl_log.h>

#ifdef HAVE_LIBNET_1_0_API
#	define	LTYPE	struct libnet_link_int
	static u_char *mk_packet(u_int32_t ip, u_char *device, u_char *macaddr, u_char *broadcast, u_char *netmask, u_short arptype);
	static int send_arp(struct libnet_link_int *l, u_char *device, u_char *buf);
#endif
#ifdef HAVE_LIBNET_1_1_API
#	define	LTYPE	libnet_t
	static libnet_t *mk_packet(libnet_t* lntag, u_int32_t ip, u_char *device, u_char macaddr[6], u_char *broadcast, u_char *netmask, u_short arptype);
	int send_arp(libnet_t* lntag);
#endif

#define PIDDIR       HA_VARRUNDIR "/" PACKAGE
#define PIDFILE_BASE PIDDIR "/send_arp-"

static char print_usage[]={
"send_arp: sends out custom ARP packet.\n"
"  usage: send_arp [-i repeatinterval-ms] [-r repeatcount] [-p pidfile] \\\n"
"              device src_ip_addr src_hw_addr broadcast_ip_addr netmask\n"
"\n"
"  where:\n"
"    repeatinterval-ms: timing, in milliseconds of sending arp packets\n"
"      For each ARP announcement requested, a pair of ARP packets is sent,\n"
"      an ARP request, and an ARP reply. This is becuse some systems\n"
"      ignore one or the other, and this combination gives the greatest\n"
"      chance of success.\n"
"\n"
"      Each time an ARP is sent, if another ARP will be sent then\n"
"      the code sleeps for half of repeatinterval-ms.\n"
"\n"
"    repeatcount: how many pairs of ARP packets to send.\n"
"                 See above for why pairs are sent\n"
"\n"
"    pidfile: pid file to use\n"
"\n"
"    device: netowrk interace to use\n"
"\n"
"    src_ip_addr: source ip address\n"
"\n"
"    src_hw_addr: source hardware address.\n"
"                 If \"auto\" then the address of device\n"
"\n"
"    broadcast_ip_addr: ignored\n"
"\n"
"    netmask: ignored\n"
};

static const char * SENDARPNAME = "send_arp";

static void convert_macaddr (u_char *macaddr, u_char enet_src[6]);
static int get_hw_addr(char *device, u_char mac[6]);
int write_pid_file(const char *pidfilename);
int create_pid_directory(const char *piddirectory);

#define AUTO_MAC_ADDR "auto"


#ifndef LIBNET_ERRBUF_SIZE
#	define LIBNET_ERRBUF_SIZE 256
#endif


/* 
 * For use logd, should keep identical with the same const variables defined
 * in heartbeat.h.
 */
#define ENV_PREFIX "HA_"
#define KEY_LOGDAEMON   "use_logd"

static void
byebye(int nsig)
{
	(void)nsig;
	/* Avoid an "error exit" log message if we're killed */
	exit(0);
}


int
main(int argc, char *argv[])
{
	int	c = -1;
	char	errbuf[LIBNET_ERRBUF_SIZE];
	char*	device;
	char*	ipaddr;
	char*	macaddr;
	char*	broadcast;
	char*	netmask;
	u_int32_t	ip;
	u_char  src_mac[6];
	int	repeatcount = 1;
	int	j;
	long	msinterval = 1000;
	int	flag;
	char    pidfilenamebuf[64];
	char    *pidfilename = NULL;

#ifdef HAVE_LIBNET_1_0_API
	LTYPE*	l;
	u_char *request, *reply;
#elif defined(HAVE_LIBNET_1_1_API)
	LTYPE *request, *reply;
#endif

	CL_SIGNAL(SIGTERM, byebye);
	CL_SIGINTERRUPT(SIGTERM, 1);

        cl_log_set_entity(SENDARPNAME);
        cl_log_enable_stderr(TRUE);
        cl_log_set_facility(LOG_USER);
	cl_inherit_logging_environment(0);

	while ((flag = getopt(argc, argv, "i:r:p:")) != EOF) {
		switch(flag) {

		case 'i':	msinterval= atol(optarg);
				break;

		case 'r':	repeatcount= atoi(optarg);
				break;

		case 'p':	pidfilename= optarg;
				break;

		default:	fprintf(stderr, "%s\n\n", print_usage);
				return 1;
				break;
		}
	}
	if (argc-optind != 5) {
		fprintf(stderr, "%s\n\n", print_usage);
		return 1;
	}

	/*
	 *	argv[optind+1] DEVICE		dc0,eth0:0,hme0:0,
	 *	argv[optind+2] IP		192.168.195.186
	 *	argv[optind+3] MAC ADDR		00a0cc34a878
	 *	argv[optind+4] BROADCAST	192.168.195.186
	 *	argv[optind+5] NETMASK		ffffffffffff
	 */

	device    = argv[optind];
	ipaddr    = argv[optind+1];
	macaddr   = argv[optind+2];
	broadcast = argv[optind+3];
	netmask   = argv[optind+4];

	if (!pidfilename) {
		if (snprintf(pidfilenamebuf, sizeof(pidfilenamebuf), "%s%s", 
					PIDFILE_BASE, ipaddr) >= 
				(int)sizeof(pidfilenamebuf)) {
			cl_log(LOG_INFO, "Pid file truncated");
			return EXIT_FAILURE;
		}
		pidfilename = pidfilenamebuf;
	}

	if(write_pid_file(pidfilename) < 0) {
		return EXIT_FAILURE;
	}

	if (!strcasecmp(macaddr, AUTO_MAC_ADDR)) {
		if (get_hw_addr(device, src_mac) < 0) {
			 cl_log(LOG_ERR, "Cannot find mac address for %s",
					 device);
			 unlink(pidfilename);
			 return EXIT_FAILURE;
		}
	}
	else {
		convert_macaddr((unsigned char *)macaddr, src_mac);
	}

/*
 * We need to send both a broadcast ARP request as well as the ARP response we
 * were already sending.  All the interesting research work for this fix was
 * done by Masaki Hasegawa <masaki-h@pp.iij4u.or.jp> and his colleagues.
 */

#if defined(HAVE_LIBNET_1_0_API)
#ifdef ON_DARWIN
	if ((ip = libnet_name_resolve((unsigned char*)ipaddr, 1)) == -1UL) {
#else
	if ((ip = libnet_name_resolve(ipaddr, 1)) == -1UL) {
#endif
		cl_log(LOG_ERR, "Cannot resolve IP address [%s]", ipaddr);
		unlink(pidfilename);
		return EXIT_FAILURE;
	}

	l = libnet_open_link_interface(device, errbuf);
	if (!l) {
		cl_log(LOG_ERR, "libnet_open_link_interface on %s: %s"
		,	device, errbuf);
		unlink(pidfilename);
		return EXIT_FAILURE;
	}
	request = mk_packet(ip, (unsigned char*)device, src_mac
		, (unsigned char*)broadcast, (unsigned char*)netmask
		, ARPOP_REQUEST);
	reply = mk_packet(ip, (unsigned char*)device, src_mac
		, (unsigned char *)broadcast
		, (unsigned char *)netmask, ARPOP_REPLY);
	if (!request || !reply) {
		cl_log(LOG_ERR, "could not create packets");
		unlink(pidfilename);
		return EXIT_FAILURE;
	}
	for (j=0; j < repeatcount; ++j) {
		c = send_arp(l, (unsigned char*)device, request);
		if (c < 0) {
			break;
		}
		mssleep(msinterval / 2);
		c = send_arp(l, (unsigned char*)device, reply);
		if (c < 0) {
			break;
		}
		if (j != repeatcount-1) {
			mssleep(msinterval / 2);
		}
	}
#elif defined(HAVE_LIBNET_1_1_API)
	if ((request=libnet_init(LIBNET_LINK, device, errbuf)) == NULL) {
		cl_log(LOG_ERR, "libnet_init failure on %s: %s", device, errbuf);
		unlink(pidfilename);
		return EXIT_FAILURE;
	}
	if ((reply=libnet_init(LIBNET_LINK, device, errbuf)) == NULL) {
		cl_log(LOG_ERR, "libnet_init failure on %s: %s", device, errbuf);
		unlink(pidfilename);
		return EXIT_FAILURE;
	}
	if ((signed)(ip = libnet_name2addr4(request, ipaddr, 1)) == -1) {
		cl_log(LOG_ERR, "Cannot resolve IP address [%s]", ipaddr);
		unlink(pidfilename);
		return EXIT_FAILURE;
	}
	request = mk_packet(request, ip, (unsigned char*)device, src_mac
		, (unsigned char*)broadcast, (unsigned char*)netmask
		, ARPOP_REQUEST);
	reply = mk_packet(reply, ip, (unsigned char*)device, src_mac
		, (unsigned char *)broadcast
		, (unsigned char *)netmask, ARPOP_REPLY);
	if (!request || !reply) {
		cl_log(LOG_ERR, "could not create packets");
		unlink(pidfilename);
		return EXIT_FAILURE;
	}
	for (j=0; j < repeatcount; ++j) {
		c = send_arp(request);
		if (c < 0) {
			break;
		}
		mssleep(msinterval / 2);
		c = send_arp(reply);
		if (c < 0) {
			break;
		}
		if (j != repeatcount-1) {
			mssleep(msinterval / 2);
		}
	}
#else
#	error "Must have LIBNET API version defined."
#endif

	unlink(pidfilename);
	return c < 0  ? EXIT_FAILURE : EXIT_SUCCESS;
}


void
convert_macaddr (u_char *macaddr, u_char enet_src[6])
{
	int i, pos;
	u_char bits[3];

	pos = 0;
	for (i = 0; i < 6; i++) {
		/* Inserted to allow old-style MAC addresses */
		if (*macaddr == ':') {
			pos++;
		}
		bits[0] = macaddr[pos++];
		bits[1] = macaddr[pos++];
		bits[2] = '\0';

		enet_src[i] = strtol((const char *)bits, (char **)NULL, 16);
	}

}

#ifdef HAVE_LIBNET_1_0_API
int
get_hw_addr(char *device, u_char mac[6])
{
	struct ether_addr	*mac_address;
	struct libnet_link_int  *network;
	char                    err_buf[LIBNET_ERRBUF_SIZE];

	network = libnet_open_link_interface(device, err_buf);
	if (!network) {
		fprintf(stderr, "libnet_open_link_interface: %s\n", err_buf);
		return -1;
	}

	mac_address = libnet_get_hwaddr(network, device, err_buf);
	if (!mac_address) {
		fprintf(stderr, "libnet_get_hwaddr: %s\n", err_buf);
		return -1;
	}

	memcpy(mac, mac_address->ether_addr_octet, 6);

	return 0;
}
#endif

#ifdef HAVE_LIBNET_1_1_API
int
get_hw_addr(char *device, u_char mac[6])
{
	struct libnet_ether_addr	*mac_address;
	libnet_t		*ln;
	char			err_buf[LIBNET_ERRBUF_SIZE];

	ln = libnet_init(LIBNET_LINK, device, err_buf);
	if (!ln) {
		fprintf(stderr, "libnet_open_link_interface: %s\n", err_buf);
		return -1;
	}

	mac_address = libnet_get_hwaddr(ln);
	if (!mac_address) {
		fprintf(stderr,  "libnet_get_hwaddr: %s\n", err_buf);
		return -1;
	}

	memcpy(mac, mac_address->ether_addr_octet, 6);

	return 0;
}
#endif


/*
 * Notes on send_arp() behaviour. Horms, 15th June 2004
 *
 * 1. Target Hardware Address
 *    (In the ARP portion of the packet)
 *
 *    a) ARP Reply
 *
 *       Set to the MAC address we want associated with the VIP,
 *       as per RFC2002 (4.6).
 *
 *       Previously set to ff:ff:ff:ff:ff:ff
 *
 *    b) ARP Request
 *
 *       Set to 00:00:00:00:00:00. According to RFC2002 (4.6)
 *       this value is not used in an ARP request, so the value should
 *       not matter. However, I observed that typically (always?) this value
 *       is set to 00:00:00:00:00:00. It seems harmless enough to follow
 *       this trend.
 *
 *       Previously set to ff:ff:ff:ff:ff:ff
 *
 *  2. Source Hardware Address
 *     (Ethernet Header, not in the ARP portion of the packet)
 *
 *     Set to the MAC address of the interface that the packet is being
 *     sent to. Actually, due to the way that send_arp is called this would
 *     usually (always?) be the case anyway. Although this value should not
 *     really matter, it seems sensible to set the source address to where
 *     the packet is really coming from.  The other obvious choice would be
 *     the MAC address that is being associated for the VIP. Which was the
 *     previous values.  Again, these are typically the same thing.
 *
 *     Previously set to MAC address being associated with the VIP
 */

#ifdef HAVE_LIBNET_1_0_API
u_char *
mk_packet(u_int32_t ip, u_char *device, u_char *macaddr, u_char *broadcast, u_char *netmask, u_short arptype)
{
	u_char *buf;
	u_char *target_mac;
	u_char device_mac[6];
	u_char bcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	u_char zero_mac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};


	if (libnet_init_packet(LIBNET_ARP_H + LIBNET_ETH_H, &buf) == -1) {
	cl_log(LOG_ERR, "libnet_init_packet memory:");
		return NULL;
	}

	/* Convert ASCII Mac Address to 6 Hex Digits. */

	/* Ethernet header */
	if (get_hw_addr((char*)device, device_mac) < 0) {
		cl_log(LOG_ERR, "Cannot find mac address for %s",
				device);
		return NULL;
	}

	if (libnet_build_ethernet(bcast_mac, device_mac, ETHERTYPE_ARP, NULL, 0
	,	buf) == -1) {
		cl_log(LOG_ERR, "libnet_build_ethernet failed:");
		libnet_destroy_packet(&buf);
		return NULL;
	}

	if (arptype == ARPOP_REQUEST) {
		target_mac = zero_mac;
	}
	else if (arptype == ARPOP_REPLY) {
		target_mac = macaddr;
	}
	else {
		cl_log(LOG_ERR, "unknown arptype");
		return NULL;
	}

	/*
	 *  ARP header
	 */
	if (libnet_build_arp(ARPHRD_ETHER,	/* Hardware address type */
		ETHERTYPE_IP,			/* Protocol address type */
		6,				/* Hardware address length */
		4,				/* Protocol address length */
		arptype,			/* ARP operation */
		macaddr,			/* Source hardware addr */
		(u_char *)&ip,			/* Target hardware addr */
		target_mac,			/* Destination hw addr */
		(u_char *)&ip,			/* Target protocol address */
		NULL,				/* Payload */
		0,				/* Payload length */
		buf + LIBNET_ETH_H) == -1) {
	        cl_log(LOG_ERR, "libnet_build_arp failed:");
		libnet_destroy_packet(&buf);
		return NULL;
	}
	return buf;
}
#endif /* HAVE_LIBNET_1_0_API */




#ifdef HAVE_LIBNET_1_1_API
libnet_t*
mk_packet(libnet_t* lntag, u_int32_t ip, u_char *device, u_char macaddr[6], u_char *broadcast, u_char *netmask, u_short arptype)
{
	u_char *target_mac;
	u_char device_mac[6];
	u_char bcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	u_char zero_mac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	if (arptype == ARPOP_REQUEST) {
		target_mac = zero_mac;
	}
	else if (arptype == ARPOP_REPLY) {
		target_mac = macaddr;
	}
	else {
		cl_log(LOG_ERR, "unkonwn arptype:");
		return NULL;
	}

	/*
	 *  ARP header
	 */
	if (libnet_build_arp(ARPHRD_ETHER,	/* hardware address type */
		ETHERTYPE_IP,	/* protocol address type */
		6,		/* Hardware address length */
		4,		/* protocol address length */
		arptype,	/* ARP operation type */
		macaddr,	/* sender Hardware address */
		(u_int8_t *)&ip,	/* sender protocol address */
		target_mac,	/* target hardware address */
		(u_int8_t *)&ip,	/* target protocol address */
		NULL,		/* Payload */
		0,		/* Length of payload */
		lntag,		/* libnet context pointer */
		0		/* packet id */
	) == -1 ) {
		cl_log(LOG_ERR, "libnet_build_arp failed:");
		return NULL;
	}

	/* Ethernet header */
	if (get_hw_addr((char *)device, device_mac) < 0) {
		cl_log(LOG_ERR, "Cannot find mac address for %s",
				device);
		return NULL;
	}

	if (libnet_build_ethernet(bcast_mac, device_mac, ETHERTYPE_ARP, NULL, 0
	,	lntag, 0) == -1 ) {
		cl_log(LOG_ERR, "libnet_build_ethernet failed:");
		return NULL;
	}
	return lntag;
}
#endif /* HAVE_LIBNET_1_1_API */

#ifdef HAVE_LIBNET_1_0_API
int
send_arp(struct libnet_link_int *l, u_char *device, u_char *buf)
{
	int n;

	n = libnet_write_link_layer(l, (char*)device, buf, LIBNET_ARP_H + LIBNET_ETH_H);
	if (n == -1) {
		cl_log(LOG_ERR, "libnet_write_link_layer failed");
	}
	return (n);
}
#endif /* HAVE_LIBNET_1_0_API */

#ifdef HAVE_LIBNET_1_1_API
int
send_arp(libnet_t* lntag)
{
	int n;

	n = libnet_write(lntag);
	if (n == -1) {
		cl_log(LOG_ERR, "libnet_write failed");
	}
	return (n);
}
#endif /* HAVE_LIBNET_1_1_API */


int
create_pid_directory(const char *pidfilename)  
{
	int	status;
	struct stat stat_buf;
	char    *pidfilename_cpy;
	char    *dir;

	pidfilename_cpy = strdup(pidfilename);
	if (!pidfilename_cpy) {
		cl_log(LOG_INFO, "Memory allocation failure: %s\n",
				strerror(errno));
		return -1;
	}
	
	dir = dirname(pidfilename_cpy);

	status = stat(dir, &stat_buf); 

	if (status < 0 && errno != ENOENT && errno != ENOTDIR) {
		cl_log(LOG_INFO, "Could not stat pid-file directory "
				"[%s]: %s", dir, strerror(errno));
		free(pidfilename_cpy);
		return -1;
	}
	
	if (status >= 0) {
		if (S_ISDIR(stat_buf.st_mode)) {
			return 0;
		}
		cl_log(LOG_INFO, "Pid-File directory exists but is "
				"not a directory [%s]", dir);
		free(pidfilename_cpy);
		return -1;
        }

	if (mkdir(dir, S_IRUSR|S_IWUSR|S_IXUSR | S_IRGRP|S_IXGRP) < 0) {
		/* Did someone else make it while we were trying ? */
		if (errno == EEXIST && stat(dir, &stat_buf) >= 0
		&&	S_ISDIR(stat_buf.st_mode)) {
			return 0;
		}
		cl_log(LOG_INFO, "Could not create pid-file directory "
				"[%s]: %s", dir, strerror(errno));
		free(pidfilename_cpy);
		return -1;
	}

	free(pidfilename_cpy);
	return 0;
}


int
write_pid_file(const char *pidfilename)  
{

	int     	pidfilefd;
	char    	pidbuf[11];
	unsigned long   pid;
	ssize_t 	bytes;

	if (*pidfilename != '/') {
		cl_log(LOG_INFO, "Invalid pid-file name, must begin with a "
				"'/' [%s]\n", pidfilename);
		return -1;
	}

	if (create_pid_directory(pidfilename) < 0) {
		return -1;
	}

	while (1) {
		pidfilefd = open(pidfilename, O_CREAT|O_EXCL|O_RDWR, 
				S_IRUSR|S_IWUSR);
		if (pidfilefd < 0) {
			if (errno != EEXIST) { /* Old PID file */
				cl_log(LOG_INFO, "Could not open pid-file "
						"[%s]: %s", pidfilename, 
						strerror(errno));
				return -1;
			}
		}
		else {
			break;
		}

		pidfilefd = open(pidfilename, O_RDONLY, S_IRUSR|S_IWUSR);
		if (pidfilefd < 0) {
			cl_log(LOG_INFO, "Could not open pid-file " 
					"[%s]: %s", pidfilename, 
					strerror(errno));
			return -1;
		}

		while (1) {
			bytes = read(pidfilefd, pidbuf, sizeof(pidbuf)-1);
			if (bytes < 0) {
				if (errno == EINTR) {
					continue;
				}
				cl_log(LOG_INFO, "Could not read pid-file " 
						"[%s]: %s", pidfilename, 
						strerror(errno));
				return -1;
			}
			pidbuf[bytes] = '\0';
			break;
		}

		if(unlink(pidfilename) < 0) {
			cl_log(LOG_INFO, "Could not delete pid-file "
	 				"[%s]: %s", pidfilename, 
					strerror(errno));
			return -1;
		}

		if (!bytes) {
			cl_log(LOG_INFO, "Invalid pid in pid-file "
	 				"[%s]: %s", pidfilename, 
					strerror(errno));
			return -1;
		}

		close(pidfilefd);

		pid = strtoul(pidbuf, NULL, 10);
		if (pid == ULONG_MAX && errno == ERANGE) {
			cl_log(LOG_INFO, "Invalid pid in pid-file "
	 				"[%s]: %s", pidfilename, 
					strerror(errno));
			return -1;
		}

		if (kill(pid, SIGKILL) < 0 && errno != ESRCH) {
			cl_log(LOG_INFO, "Error killing old proccess [%lu] "
	 				"from pid-file [%s]: %s", pid,
					pidfilename, strerror(errno));
			return -1;
		}

		cl_log(LOG_INFO, "Killed old send_arp process [%lu]\n",
				pid);
	}

	if (snprintf(pidbuf, sizeof(pidbuf), "%u"
	,	getpid()) >= (int)sizeof(pidbuf)) {
		cl_log(LOG_INFO, "Pid too long for buffer [%u]", getpid());
		return -1;
	}

	while (1) {
		bytes = write(pidfilefd, pidbuf, strlen(pidbuf));
		if (bytes != (ssize_t)strlen(pidbuf)) {
			if (bytes < 0 && errno == EINTR) {
				continue;
			}
			cl_log(LOG_INFO, "Could not write pid-file "
					"[%s]: %s", pidfilename,
					strerror(errno));
			return -1;
		}
		break;
	}

	close(pidfilefd);

	return 0;
}


