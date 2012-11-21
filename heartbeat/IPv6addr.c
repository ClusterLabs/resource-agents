
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
 
/*
 * It can add an IPv6 address, or remove one.
 *
 * Usage:  IPv6addr {start|stop|status|monitor|meta-data}
 *
 * The "start" arg adds an IPv6 address.
 * The "stop" arg removes one.
 * The "status" arg shows whether the IPv6 address exists
 * The "monitor" arg shows whether the IPv6 address can be pinged (ICMPv6 ECHO)
 * The "meta_data" arg shows the meta data(XML)
 */
 
/*
 * ipv6-address:
 *
 * currently the following forms are legal:
 *	address
 *	address/prefix
 *
 *     E.g.
 *	3ffe:ffff:0:f101::3
 *	3ffe:ffff:0:f101::3/64
 *
 * It should be passed by environment variant:
 *	OCF_RESKEY_ipv6addr=3ffe:ffff:0:f101::3
 *	OCF_RESKEY_cidr_netmask=64
 *	OCF_RESKEY_nic=eth0
 *
 */
 
/*
 * start:
 * 	1.IPv6addr will choice a proper interface for the new address.
 *	2.Then assign the new address to the interface.
 *	3.Wait until the new address is available (reply ICMPv6 ECHO packet)
 *	4.Send out the unsolicited advertisements.
 *
 *	return 0(OCF_SUCCESS) for success
 *	return 1(OCF_ERR_GENERIC) for failure
 *	return 2(OCF_ERR_ARGS) for invalid or excess argument(s)
 *
 *
 * stop:
 *	remove the address from the inferface.
 *
 *	return 0(OCF_SUCCESS) for success
 *	return 1(OCF_ERR_GENERIC) for failure
 *	return 2(OCF_ERR_ARGS) for invalid or excess argument(s)
 *
 * status:
 *	return the status of the address. only check whether it exists.
 *
 *	return 0(OCF_SUCCESS) for existing
 *	return 1(OCF_NOT_RUNNING) for not existing
 *	return 2(OCF_ERR_ARGS) for invalid or excess argument(s)
 *
 *
 * monitor:
 *	ping the address by ICMPv6 ECHO request.
 *
 *	return 0(OCF_SUCCESS) for response correctly.
 *	return 1(OCF_NOT_RUNNING) for no response.
 *	return 2(OCF_ERR_ARGS) for invalid or excess argument(s)
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/icmp6.h>
#include <arpa/inet.h> /* for inet_pton */
#include <net/if.h> /* for if_nametoindex */
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libgen.h>
#include <syslog.h>
#include <signal.h>
#include <errno.h>
#include <clplumbing/cl_log.h>


#define PIDFILE_BASE HA_RSCTMPDIR  "/IPv6addr-"

/*
0	No error, action succeeded completely
1 	generic or unspecified error (current practice)
	The "monitor" operation shall return this for a crashed, hung or
	otherwise non-functional resource.
2 	invalid or excess argument(s)
	Likely error code for validate-all, if the instance parameters
	do not validate. Any other action is free to also return this
	exit status code for this case.
3 	unimplemented feature (for example, "reload")
4 	user had insufficient privilege
5 	program is not installed
6 	program is not configured
7 	program is not running
8	resource is running in "master" mode and fully operational
9	resource is in "master" mode but in a failed state
*/
#define	OCF_SUCCESS		0
#define	OCF_ERR_GENERIC		1
#define	OCF_ERR_ARGS		2
#define	OCF_ERR_UNIMPLEMENTED	3
#define	OCF_ERR_PERM		4
#define	OCF_ERR_INSTALLED	5
#define	OCF_ERR_CONFIGURED	6
#define	OCF_NOT_RUNNING		7

const char* IF_INET6	 	= "/proc/net/if_inet6";
const char* APP_NAME		= "IPv6addr";
const char* APP_NAME_SUA	= "send_ua";

const char*	START_CMD 	= "start";
const char*	STOP_CMD  	= "stop";
const char*	STATUS_CMD 	= "status";
const char*	MONITOR_CMD 	= "monitor";
const char*	ADVT_CMD	= "advt";
const char*	RECOVER_CMD 	= "recover";
const char*	RELOAD_CMD 	= "reload";
const char*	META_DATA_CMD 	= "meta-data";
const char*	VALIDATE_CMD 	= "validate-all";

char		BCAST_ADDR[]	= "ff02::1";
const int	UA_REPEAT_COUNT	= 5;
const int	QUERY_COUNT	= 5;

#define 	HWADDR_LEN 	6 /* mac address length */

struct in6_ifreq {
	struct in6_addr ifr6_addr;
	uint32_t ifr6_prefixlen;
	unsigned int ifr6_ifindex;
};

static int start_addr6(struct in6_addr* addr6, int prefix_len, char* prov_ifname);
static int stop_addr6(struct in6_addr* addr6, int prefix_len, char* prov_ifname);
static int status_addr6(struct in6_addr* addr6, int prefix_len, char* prov_ifname);
static int monitor_addr6(struct in6_addr* addr6, int prefix_len);
static int advt_addr6(struct in6_addr* addr6, int prefix_len, char* prov_ifname);
static int meta_data_addr6(void);


static void usage(const char* self);
static void usage_send_ua(const char* self);
int write_pid_file(const char *pid_file);
int create_pid_directory(const char *pid_file);
static void byebye(int nsig);

static char* scan_if(struct in6_addr* addr_target, int* plen_target,
		     int use_mask, char* prov_ifname);
static char* find_if(struct in6_addr* addr_target, int* plen_target, char* prov_ifname);
static char* get_if(struct in6_addr* addr_target, int* plen_target, char* prov_ifname);
static int assign_addr6(struct in6_addr* addr6, int prefix_len, char* if_name);
static int unassign_addr6(struct in6_addr* addr6, int prefix_len, char* if_name);
int is_addr6_available(struct in6_addr* addr6);
static int send_ua(struct in6_addr* src_ip, char* if_name);

int
main(int argc, char* argv[])
{
	char		pid_file[256];
	char*		ipv6addr;
	char*		cidr_netmask;
	int		ret;
	int		count = UA_REPEAT_COUNT;
	int		interval = 1000;	/* default 1000 msec */
	int		senduaflg = 0;
	int		ch;
	int		i;
	char*		cp;
	char*		prov_ifname = NULL;
	int		prefix_len = -1;
	struct in6_addr	addr6;

	/* Check binary name */
	if (strcmp(basename(argv[0]), APP_NAME_SUA) == 0) {
		senduaflg = 1;
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
	}

	/* Check the count of parameters first */
	if (argc < 2) {
		usage(argv[0]);
		return OCF_ERR_ARGS;
	}

	/* set termination signal */
	siginterrupt(SIGTERM, 1);
	signal(SIGTERM, byebye);

	/* open system log */
	if (senduaflg) {
		cl_log_set_entity(APP_NAME_SUA);
	} else {
		cl_log_set_entity(APP_NAME);
	}
	cl_log_set_facility(LOG_DAEMON);

	/* the meta-data dont need any parameter */
	if (0 == strncmp(META_DATA_CMD, argv[1], strlen(META_DATA_CMD))) {
		ret = meta_data_addr6();
		return OCF_SUCCESS;
	}

	if (senduaflg) {
		ipv6addr = argv[optind];
	} else {
		/* check the OCF_RESKEY_ipv6addr parameter, should be an IPv6 address */
		ipv6addr = getenv("OCF_RESKEY_ipv6addr");
	}
	if (ipv6addr == NULL) {
		cl_log(LOG_ERR, "Please set OCF_RESKEY_ipv6addr to the IPv6 address you want to manage.");
		usage(argv[0]);
		return OCF_ERR_ARGS;
	}

	/* legacy option */
	if ((cp = strchr(ipv6addr, '/'))) {
		prefix_len = atol(cp + 1);
		if ((prefix_len < 0) || (prefix_len > 128)) {
			cl_log(LOG_ERR, "Invalid prefix_len [%s], should be an integer in [0, 128]", cp+1);
			usage(argv[0]);
			return OCF_ERR_ARGS;
		}
		*cp=0;
	}

	if (senduaflg) {
		cidr_netmask = argv[optind+1];
	} else {
		/* get provided netmask (optional) */
		cidr_netmask = getenv("OCF_RESKEY_cidr_netmask");
	}
	if (cidr_netmask != NULL) {
		if ((atol(cidr_netmask) < 0) || (atol(cidr_netmask) > 128)) {
			cl_log(LOG_ERR, "Invalid prefix_len [%s], "
				"should be an integer in [0, 128]", cidr_netmask);
			usage(argv[0]);
			return OCF_ERR_ARGS;
		}
		if (prefix_len != -1 && prefix_len != atol(cidr_netmask)) {
			cl_log(LOG_DEBUG, "prefix_len(%d) is overwritted by cidr_netmask(%s)",
				prefix_len, cidr_netmask);
		}
		prefix_len = atol(cidr_netmask);

	} else if (prefix_len == -1) {
		prefix_len = 0;
	}

	if (senduaflg) {
		prov_ifname = argv[optind+2];
	} else {
		/* get provided interface name (optional) */
		prov_ifname = getenv("OCF_RESKEY_nic");
	}
	if (inet_pton(AF_INET6, ipv6addr, &addr6) <= 0) {
		cl_log(LOG_ERR, "Invalid IPv6 address [%s]", ipv6addr);
		usage(argv[0]);
		return OCF_ERR_ARGS;
	}

	/* Check whether this system supports IPv6 */
	if (access(IF_INET6, R_OK)) {
		cl_log(LOG_ERR, "No support for INET6 on this system.");
		return OCF_ERR_GENERIC;
	}

	if (senduaflg) {
		/* Send unsolicited advertisement packet to neighbor */
		for (i = 0; i < count; i++) {
			send_ua(&addr6, prov_ifname);
			usleep(interval * 1000);
		}
		return OCF_SUCCESS;
	}

	/* create the pid file so we can make sure that only one IPv6addr
	 * for this address is running
	 */
	if (snprintf(pid_file, sizeof(pid_file), "%s%s", PIDFILE_BASE, ipv6addr)
		>= (int)sizeof(pid_file)) {
		cl_log(LOG_ERR, "Pid file truncated");
		return OCF_ERR_GENERIC;
	}

	if (write_pid_file(pid_file) < 0) {
		return OCF_ERR_GENERIC;
	}


	/* switch the command */
	if (0 == strncmp(START_CMD,argv[1], strlen(START_CMD))) {
		ret = start_addr6(&addr6, prefix_len, prov_ifname);
	}else if (0 == strncmp(STOP_CMD,argv[1], strlen(STOP_CMD))) {
		ret = stop_addr6(&addr6, prefix_len, prov_ifname);
	}else if (0 == strncmp(STATUS_CMD,argv[1], strlen(STATUS_CMD))) {
		ret = status_addr6(&addr6, prefix_len, prov_ifname);
	}else if (0 ==strncmp(MONITOR_CMD,argv[1], strlen(MONITOR_CMD))) {
		ret = monitor_addr6(&addr6, prefix_len);
	}else if (0 ==strncmp(RELOAD_CMD,argv[1], strlen(RELOAD_CMD))) {
		ret = OCF_ERR_UNIMPLEMENTED;
	}else if (0 ==strncmp(RECOVER_CMD,argv[1], strlen(RECOVER_CMD))) {
		ret = OCF_ERR_UNIMPLEMENTED;
	}else if (0 ==strncmp(VALIDATE_CMD,argv[1], strlen(VALIDATE_CMD))) {
	/* ipv6addr has been validated by inet_pton, hence a valid IPv6 address */
		ret = OCF_SUCCESS;
	}else if (0 ==strncmp(ADVT_CMD,argv[1], strlen(MONITOR_CMD))) {
		ret = advt_addr6(&addr6, prefix_len, prov_ifname);
	}else{
		usage(argv[0]);
		ret = OCF_ERR_ARGS;
	}

	/* release the pid file */
	unlink(pid_file);

	return ret;
}
int
start_addr6(struct in6_addr* addr6, int prefix_len, char* prov_ifname)
{
	int	i;
	char*	if_name;
	if(OCF_SUCCESS == status_addr6(addr6,prefix_len,prov_ifname)) {
		return OCF_SUCCESS;
	}

	/* we need to find a proper device to assign the address */
	if_name = find_if(addr6, &prefix_len, prov_ifname);
	if (NULL == if_name) {
		cl_log(LOG_ERR, "no valid mechanisms");
		return OCF_ERR_GENERIC;
	}

	/* Assign the address */
	if (0 != assign_addr6(addr6, prefix_len, if_name)) {
		cl_log(LOG_ERR, "failed to assign the address to %s", if_name);
		return OCF_ERR_GENERIC;
	}

	/* Check whether the address available */
	for (i = 0; i < QUERY_COUNT; i++) {
		if (0 == is_addr6_available(addr6)) {
			break;
		}
		sleep(1);
	}
	if (i == QUERY_COUNT) {
		cl_log(LOG_ERR, "failed to ping the address");
		return OCF_ERR_GENERIC;
	}

	/* Send unsolicited advertisement packet to neighbor */
	for (i = 0; i < UA_REPEAT_COUNT; i++) {
		send_ua(addr6, if_name);
		sleep(1);
	}
	return OCF_SUCCESS;
}

int
advt_addr6(struct in6_addr* addr6, int prefix_len, char* prov_ifname)
{
	/* First, we need to find a proper device to assign the address */
	char*	if_name = get_if(addr6, &prefix_len, prov_ifname);
	int	i;
	if (NULL == if_name) {
		cl_log(LOG_ERR, "no valid mechanisms");
		return OCF_ERR_GENERIC;
	}
	/* Send unsolicited advertisement packet to neighbor */
	for (i = 0; i < UA_REPEAT_COUNT; i++) {
		send_ua(addr6, if_name);
		sleep(1);
	}
	return OCF_SUCCESS;
}

int
stop_addr6(struct in6_addr* addr6, int prefix_len, char* prov_ifname)
{
	char* if_name;
	if(OCF_NOT_RUNNING == status_addr6(addr6,prefix_len,prov_ifname)) {
		return OCF_SUCCESS;
	}

	if_name = get_if(addr6, &prefix_len, prov_ifname);

	if (NULL == if_name) {
		cl_log(LOG_ERR, "no valid mechanisms.");
		/* I think this should be a success exit according to LSB. */
		return OCF_ERR_GENERIC;
	}

	/* Unassign the address */
	if (0 != unassign_addr6(addr6, prefix_len, if_name)) {
		cl_log(LOG_ERR, "failed to assign the address to %s", if_name);
		return OCF_ERR_GENERIC;
	}

	return OCF_SUCCESS;
}

int
status_addr6(struct in6_addr* addr6, int prefix_len, char* prov_ifname)
{
	char* if_name = get_if(addr6, &prefix_len, prov_ifname);
	if (NULL == if_name) {
		return OCF_NOT_RUNNING;
	}
	return OCF_SUCCESS;
}

int
monitor_addr6(struct in6_addr* addr6, int prefix_len)
{
	if(0 == is_addr6_available(addr6)) {
		return OCF_SUCCESS;
	}
	return OCF_NOT_RUNNING;
}

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
		cl_log(LOG_ERR, "socket(IPPROTO_ICMPV6) failed: %s",
		       strerror(errno));
		return status;
	}
	/* set the outgoing interface */
	ifindex = if_nametoindex(if_name);
	if (setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_IF,
		       &ifindex, sizeof(ifindex)) < 0) {
		cl_log(LOG_ERR, "setsockopt(IPV6_MULTICAST_IF) failed: %s",
		       strerror(errno));
		goto err;
	}
	/* set the hop limit */
	hop = 255; /* 255 is required. see rfc4861 7.1.2 */
	if (setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
		       &hop, sizeof(hop)) < 0) {
		cl_log(LOG_ERR, "setsockopt(IPV6_MULTICAST_HOPS) failed: %s",
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
		cl_log(LOG_ERR, "bind() failed: %s", strerror(errno));
		goto err;
	}


	/* get the hardware address */
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, if_name, sizeof(ifr.ifr_name) - 1);
	if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) {
		cl_log(LOG_ERR, "ioctl(SIOCGIFHWADDR) failed: %s", strerror(errno));
		goto err;
	}

	/* build a neighbor advertisement message */
	payload_size = sizeof(struct nd_neighbor_advert)
			 + sizeof(struct nd_opt_hdr) + HWADDR_LEN;
	payload = memalign(sysconf(_SC_PAGESIZE), payload_size);
	if (!payload) {
		cl_log(LOG_ERR, "malloc for payload failed");
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
		cl_log(LOG_ERR, "sendto(%s) failed: %s",
		       if_name, strerror(errno));
		goto err;
	}

	status = 0;

err:
	close(fd);
	free(payload);
	return status;
}

/* find the network interface associated with an address */
char*
scan_if(struct in6_addr* addr_target, int* plen_target, int use_mask, char* prov_ifname)
{
	FILE *f;
	static char devname[21]="";
	struct in6_addr addr;
	struct in6_addr mask;
	unsigned int plen, scope, dad_status, if_idx;
	unsigned int addr6p[4];

	/* open /proc/net/if_inet6 file */
	if ((f = fopen(IF_INET6, "r")) == NULL) {
		return NULL;
	}

	/* Loop for each entry */
	while (1) {
		int		i;
		int		n;
		int		s;
		gboolean	same = TRUE;

		i = fscanf(f, "%08x%08x%08x%08x %x %02x %02x %02x %20s\n",
		       	   &addr6p[0], &addr6p[1], &addr6p[2], &addr6p[3],
			   &if_idx, &plen, &scope, &dad_status, devname);
		if (i == EOF) {
			break;
		}
		else if (i != 9) {
			cl_log(LOG_INFO, "Error parsing %s, "
			       "perhaps the format has changed\n", IF_INET6);
			break;
		}

		/* Consider link-local addresses (scope == 0x20) only when
		 * the inerface name is provided, and global addresses
		 * (scope == 0). Skip everything else.
		 */
		if (scope != 0) {
			if (scope != 0x20 || prov_ifname == 0
				|| *prov_ifname == 0)
				continue;
		}

		/* If specified prefix, only same prefix entry
		 * would be considered.
		 */
		if (*plen_target!=0 && plen != *plen_target) {
			continue;
		}

		/* If interface name provided, only same devname entry
		 * would be considered
		 */
		if (prov_ifname!=0 && *prov_ifname!=0)
		{
			if (strcmp(devname, prov_ifname))
				continue;
		}

		for (i = 0; i< 4; i++) {
			addr.s6_addr32[i] = htonl(addr6p[i]);
		}

		/* Make the mask based on prefix length */
		memset(mask.s6_addr, 0xff, 16);
		if (use_mask && plen < 128) {
			n = plen / 32;
			memset(mask.s6_addr32 + n + 1, 0, (3 - n) * 4);
			s = 32 - plen % 32;
			if (s == 32) 
				mask.s6_addr32[n] = 0x0;
			else
				mask.s6_addr32[n] = 0xffffffff << s;
			mask.s6_addr32[n] = htonl(mask.s6_addr32[n]);
		}

		/* compare addr and addr_target */
		same = TRUE;
		for (i = 0; i < 4; i++) {
			if ((addr.s6_addr32[i]&mask.s6_addr32[i]) !=
			    (addr_target->s6_addr32[i]&mask.s6_addr32[i])) {
				same = FALSE;
				break;
			}
		}
		
		/* We found it!	*/
		if (same) {
			fclose(f);
			*plen_target = plen;
			return devname;
		}
	}
	fclose(f);
	return NULL;
}
/* find a proper network interface to assign the address */
char*
find_if(struct in6_addr* addr_target, int* plen_target, char* prov_ifname)
{
	char *best_ifname = scan_if(addr_target, plen_target, 1, prov_ifname);

	/* use the provided ifname and prefix if the address did not match */
	if (best_ifname == NULL &&
	    prov_ifname != 0 &&  *prov_ifname != 0 && *plen_target != 0) {
		cl_log(LOG_INFO, "Could not find a proper interface by the ipv6addr. Using the specified nic:'%s' and cidr_netmask:'%d'", prov_ifname, *plen_target);
		return prov_ifname;
	}
	return best_ifname;
}
/* get the device name and the plen_target of a special address */
char*
get_if(struct in6_addr* addr_target, int* plen_target, char* prov_ifname)
{
	return scan_if(addr_target, plen_target, 0, prov_ifname);
}
int
assign_addr6(struct in6_addr* addr6, int prefix_len, char* if_name)
{
	struct in6_ifreq ifr6;

	/* Get socket first */
	int		fd;
	struct ifreq	ifr;

	fd = socket(AF_INET6, SOCK_DGRAM, 0);
	if (fd < 0) {
		return 1;
	}

	/* Query the index of the if */
	strcpy(ifr.ifr_name, if_name);
	if (ioctl(fd, SIOGIFINDEX, &ifr) < 0) {
		return -1;
	}

	/* Assign the address to the if */
	ifr6.ifr6_addr = *addr6;
	ifr6.ifr6_ifindex = ifr.ifr_ifindex;
	ifr6.ifr6_prefixlen = prefix_len;
	if (ioctl(fd, SIOCSIFADDR, &ifr6) < 0) {
		return -1;
	}
	close (fd);
	return 0;
}
int
unassign_addr6(struct in6_addr* addr6, int prefix_len, char* if_name)
{
	int			fd;
	struct ifreq		ifr;
	struct in6_ifreq	ifr6;

	/* Get socket first */
	fd = socket(AF_INET6, SOCK_DGRAM, 0);
	if (fd < 0) {
		return 1;
	}

	/* Query the index of the if */
	strcpy(ifr.ifr_name, if_name);
	if (ioctl(fd, SIOGIFINDEX, &ifr) < 0) {
		return -1;
	}

	/* Unassign the address to the if */
	ifr6.ifr6_addr = *addr6;
	ifr6.ifr6_ifindex = ifr.ifr_ifindex;
	ifr6.ifr6_prefixlen = prefix_len;
	if (ioctl(fd, SIOCDIFADDR, &ifr6) < 0) {
		return -1;
	}
	
	close (fd);
	return 0;
}

#define	MINPACKSIZE	64
int
is_addr6_available(struct in6_addr* addr6)
{
	struct sockaddr_in6		addr;
	struct icmp6_hdr		icmph;
	u_char				outpack[MINPACKSIZE];
	int				icmp_sock;
	int				ret;
	struct iovec			iov;
	u_char				packet[MINPACKSIZE];
	struct msghdr			msg;

	if ((icmp_sock = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6)) == -1) {
		return -1;
	}

	memset(&icmph, 0, sizeof(icmph));
	icmph.icmp6_type = ICMP6_ECHO_REQUEST;
	icmph.icmp6_code = 0;
	icmph.icmp6_cksum = 0;
	icmph.icmp6_seq = htons(0);
	icmph.icmp6_id = 0;

	memset(&outpack, 0, sizeof(outpack));
	memcpy(&outpack, &icmph, sizeof(icmph));

	memset(&addr, 0, sizeof(struct sockaddr_in6));
	addr.sin6_family = AF_INET6;
	addr.sin6_port = htons(IPPROTO_ICMPV6);
	memcpy(&addr.sin6_addr,addr6,sizeof(struct in6_addr));

	/* Only the first 8 bytes of outpack are meaningful... */
	ret = sendto(icmp_sock, (char *)outpack, sizeof(outpack), 0,
			   (struct sockaddr *) &addr,
			   sizeof(struct sockaddr_in6));
	if (0 >= ret) {
		return -1;
	}

	iov.iov_base = (char *)packet;
	iov.iov_len = sizeof(packet); 

	msg.msg_name = &addr;
	msg.msg_namelen = sizeof(addr);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;

	ret = recvmsg(icmp_sock, &msg, MSG_DONTWAIT);
	if (0 >= ret) {
		return -1;
	}
	
	return 0;
}

static void usage(const char* self)
{
	printf("usage: %s {start|stop|status|monitor|validate-all|meta-data}\n",self);
	return;
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

int
create_pid_directory(const char *pid_file)
{
	int status;
	int return_status = -1;
	struct stat stat_buf;
	char* dir;

	dir = strdup(pid_file);
	if (!dir) {
		cl_log(LOG_INFO, "Memory allocation failure: %s",
				strerror(errno));
		return -1;
	}

	dirname(dir);

	status = stat(dir, &stat_buf);

	if (status < 0 && errno != ENOENT && errno != ENOTDIR) {
		cl_log(LOG_INFO, "Could not stat pid-file directory "
				"[%s]: %s", dir, strerror(errno));
		goto err;
	}

	if (!status) {
		if (S_ISDIR(stat_buf.st_mode)) {
			goto out;
		}
		cl_log(LOG_INFO, "Pid-File directory exists but is "
				"not a directory [%s]", dir);
		goto err;
        }

	if (mkdir(dir, S_IRUSR|S_IWUSR|S_IXUSR | S_IRGRP|S_IXGRP) < 0) {
		cl_log(LOG_INFO, "Could not create pid-file directory "
				"[%s]: %s", dir, strerror(errno));
		goto err;
	}

out:
	return_status = 0;
err:
	free(dir);
	return return_status;
}

int
write_pid_file(const char *pid_file)
{

	int     	pidfilefd;
	char    	pidbuf[11];
	unsigned long   pid;
	ssize_t 	bytes;

	if (*pid_file != '/') {
		cl_log(LOG_INFO, "Invalid pid-file name, must begin with a "
				"'/' [%s]\n", pid_file);
		return -1;
	}

	if (create_pid_directory(pid_file) < 0) {
		return -1;
	}

	while (1) {
		pidfilefd = open(pid_file, O_CREAT|O_EXCL|O_RDWR,
				S_IRUSR|S_IWUSR);
		if (pidfilefd < 0) {
			if (errno != EEXIST) { /* Old PID file */
				cl_log(LOG_INFO, "Could not open pid-file "
						"[%s]: %s", pid_file,
						strerror(errno));
				return -1;
			}
		}
		else {
			break;
		}

		pidfilefd = open(pid_file, O_RDONLY, S_IRUSR|S_IWUSR);
		if (pidfilefd < 0) {
			cl_log(LOG_INFO, "Could not open pid-file "
					"[%s]: %s", pid_file,
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
						"[%s]: %s", pid_file,
						strerror(errno));
				return -1;
			}
			pidbuf[bytes] = '\0';
			break;
		}

		if(unlink(pid_file) < 0) {
			cl_log(LOG_INFO, "Could not delete pid-file "
	 				"[%s]: %s", pid_file,
					strerror(errno));
			return -1;
		}

		if (!bytes) {
			cl_log(LOG_INFO, "Invalid pid in pid-file "
	 				"[%s]: %s", pid_file,
					strerror(errno));
			return -1;
		}

		close(pidfilefd);

		pid = strtoul(pidbuf, NULL, 10);
		if (pid == ULONG_MAX && errno == ERANGE) {
			cl_log(LOG_INFO, "Invalid pid in pid-file "
	 				"[%s]: %s", pid_file,
					strerror(errno));
			return -1;
		}

		if (kill(pid, SIGKILL) < 0 && errno != ESRCH) {
			cl_log(LOG_INFO, "Error killing old proccess [%lu] "
	 				"from pid-file [%s]: %s", pid,
					pid_file, strerror(errno));
			return -1;
		}

		cl_log(LOG_INFO, "Killed old send_arp process [%lu]", pid);
	}

	if (snprintf(pidbuf, sizeof(pidbuf), "%u"
	,	getpid()) >= (int)sizeof(pidbuf)) {
		cl_log(LOG_INFO, "Pid too long for buffer [%u]", getpid());
		return -1;
	}

	while (1) {
		bytes = write(pidfilefd, pidbuf, strlen(pidbuf));
		if (bytes != strlen(pidbuf)) {
			if (bytes < 0 && errno == EINTR) {
				continue;
			}
			cl_log(LOG_INFO, "Could not write pid-file "
					"[%s]: %s", pid_file,
					strerror(errno));
			return -1;
		}
		break;
	}

	close(pidfilefd);

	return 0;
}
static int
meta_data_addr6(void)
{
	const char* meta_data=
	"<?xml version=\"1.0\"?>\n"
	"<!DOCTYPE resource-agent SYSTEM \"ra-api-1.dtd\">\n"
	"<resource-agent name=\"IPv6addr\">\n"
	"  <version>1.0</version>\n"
	"  <longdesc lang=\"en\">\n"
	"   This script manages IPv6 alias IPv6 addresses,It can add an IP6\n"
	"   alias, or remove one.\n"
	"  </longdesc>\n"
	"  <shortdesc lang=\"en\">Manages IPv6 aliases</shortdesc>\n"
	"  <parameters>\n"
	"    <parameter name=\"ipv6addr\" unique=\"0\" required=\"1\">\n"
	"      <longdesc lang=\"en\">\n"
	"	The IPv6 address this RA will manage \n"
	"      </longdesc>\n"
	"      <shortdesc lang=\"en\">IPv6 address</shortdesc>\n"
	"      <content type=\"string\" default=\"\" />\n"
	"    </parameter>\n"
	"    <parameter name=\"cidr_netmask\" unique=\"0\">\n"
	"      <longdesc lang=\"en\">\n"
	"	The netmask for the interface in CIDR format. (ie, 24).\n"
	"	The value of this parameter overwrites the value of _prefix_\n"
	"	of ipv6addr parameter.\n"
	"      </longdesc>\n"
	"      <shortdesc lang=\"en\">Netmask</shortdesc>\n"
	"      <content type=\"string\" default=\"\" />\n"
	"    </parameter>\n"
	"    <parameter name=\"nic\" unique=\"0\">\n"
	"      <longdesc lang=\"en\">\n"
	"	The base network interface on which the IPv6 address will\n"
	"	be brought online.\n"
	"      </longdesc>\n"
	"      <shortdesc lang=\"en\">Network interface</shortdesc>\n"
	"      <content type=\"string\" default=\"\" />\n"
	"    </parameter>\n"
	"  </parameters>\n"
	"  <actions>\n"
	"    <action name=\"start\"   timeout=\"15\" />\n"
	"    <action name=\"stop\"    timeout=\"15\" />\n"
	"    <action name=\"status\"  timeout=\"15\" interval=\"15\" />\n"
	"    <action name=\"monitor\" timeout=\"15\" interval=\"15\" />\n"
	"    <action name=\"validate-all\"  timeout=\"5\" />\n"
	"    <action name=\"meta-data\"  timeout=\"5\" />\n"
	"  </actions>\n"
	"</resource-agent>\n";
	printf("%s\n",meta_data);
	return OCF_SUCCESS;
}
