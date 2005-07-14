
/*
 * This program manages IPv6 address with OCF Resource Agent standard.
 *
 * Author: Huang Zhen <zhenh@cn.ibm.com>
 * Copyright (c) 2004 International Business Machines
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
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
 *
 */

#include <sys/types.h>
#include <netinet/icmp6.h>
#include <libgen.h>
#include <syslog.h>
#include <clplumbing/cl_log.h>
#include <libnet.h>


#define PIDDIR "/var/run/"
#define PIDFILE_BASE PIDDIR "/IPv6addr-"

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

struct in6_ifreq {
	struct in6_addr ifr6_addr;
	uint32_t ifr6_prefixlen;
	unsigned int ifr6_ifindex;
};

static int start_addr6(struct in6_addr* addr6, int prefix_len);
static int stop_addr6(struct in6_addr* addr6, int prefix_len);
static int status_addr6(struct in6_addr* addr6, int prefix_len);
static int monitor_addr6(struct in6_addr* addr6, int prefix_len);
static int advt_addr6(struct in6_addr* addr6, int prefix_len);
static int meta_data_addr6(void);


static void usage(const char* self);
int write_pid_file(const char *pid_file);
int create_pid_directory(const char *pid_file);
static void byebye(int nsig);

static char* find_if(struct in6_addr* addr_target, int* plen_target);
static char* get_if(struct in6_addr* addr_target, int* plen_target);
static int assign_addr6(struct in6_addr* addr6, int prefix_len, char* if_name);
static int unassign_addr6(struct in6_addr* addr6, int prefix_len, char* if_name);
int is_addr6_available(struct in6_addr* addr6);
static int send_ua(struct in6_addr* src_ip, char* if_name);

int
main(int argc, char* argv[])
{
	char		pid_file[256];
	char*		ipv6addr;
	int		ret;
	char*		cp;
	int		prefix_len;
	struct in6_addr	addr6;

	/* Check the count of parameters first */
	if (argc < 2) {
		usage(argv[0]);
		return OCF_ERR_ARGS;
	}

	/* set termination signal */
	siginterrupt(SIGTERM, 1);
	signal(SIGTERM, byebye);

	/* open system log */
	cl_log_set_entity(APP_NAME);
	cl_log_set_facility(LOG_DAEMON);

	/* the meta-data dont need any parameter */
	if (0 == strncmp(META_DATA_CMD, argv[1], strlen(META_DATA_CMD))) {
		ret = meta_data_addr6();
		return OCF_SUCCESS;
	}

	/* check the OCF_RESKEY_ipv6addr parameter, should be a IPv6 address */
	ipv6addr = getenv("OCF_RESKEY_ipv6addr");
	if (ipv6addr == NULL) {
		usage(argv[0]);
		return OCF_ERR_ARGS;
	}
	if ((cp = strchr(ipv6addr, '/'))) {
		prefix_len = atol(cp + 1);
		if ((prefix_len < 0) || (prefix_len > 128)) {
			usage(argv[0]);
			return OCF_ERR_ARGS;
		}
		*cp=0;
	} else {
		prefix_len = 0;
	}

	if (inet_pton(AF_INET6, ipv6addr, &addr6) <= 0) {
		usage(argv[0]);
		return OCF_ERR_ARGS;
	}

	/* Check whether this system supports IPv6 */
	if (access(IF_INET6, R_OK)) {
		cl_log(LOG_ERR, "No support for INET6 on this system.");
		return OCF_ERR_GENERIC;
	}

	/* create the pid file so we can make sure that only one IPv6addr
	 * for this address is running
	 */
	if (snprintf(pid_file, sizeof(pid_file), "%s%s", PIDFILE_BASE, argv[1])
		>= (int)sizeof(pid_file)) {
		cl_log(LOG_ERR, "Pid file truncated");
		return OCF_ERR_GENERIC;
	}

	if (write_pid_file(pid_file) < 0) {
		return OCF_ERR_GENERIC;
	}


	/* switch the command */
	if (0 == strncmp(START_CMD,argv[1], strlen(START_CMD))) {
		ret = start_addr6(&addr6, prefix_len);
	}else if (0 == strncmp(STOP_CMD,argv[1], strlen(STOP_CMD))) {
		ret = stop_addr6(&addr6, prefix_len);
	}else if (0 == strncmp(STATUS_CMD,argv[1], strlen(STATUS_CMD))) {
		ret = status_addr6(&addr6, prefix_len);
	}else if (0 ==strncmp(MONITOR_CMD,argv[1], strlen(MONITOR_CMD))) {
		ret = monitor_addr6(&addr6, prefix_len);
	}else if (0 ==strncmp(RELOAD_CMD,argv[1], strlen(RELOAD_CMD))) {
		ret = OCF_ERR_UNIMPLEMENTED;
	}else if (0 ==strncmp(RECOVER_CMD,argv[1], strlen(RECOVER_CMD))) {
		ret = OCF_ERR_UNIMPLEMENTED;
	}else if (0 ==strncmp(VALIDATE_CMD,argv[1], strlen(VALIDATE_CMD))) {
		ret = OCF_ERR_UNIMPLEMENTED;
	}else if (0 ==strncmp(ADVT_CMD,argv[1], strlen(MONITOR_CMD))) {
		ret = advt_addr6(&addr6, prefix_len);
	}else{
		usage(argv[0]);
		ret = OCF_ERR_ARGS;
	}

	/* release the pid file */
	unlink(pid_file);

	return ret;
}
int
start_addr6(struct in6_addr* addr6, int prefix_len)
{
	int	i;
	char*	if_name;
	if(OCF_SUCCESS == status_addr6(addr6,prefix_len)) {
		return OCF_SUCCESS;
	}

	/* we need to find a proper device to assign the address */
	if_name = find_if(addr6, &prefix_len);
	if (NULL == if_name) {
		cl_log(LOG_ERR, "no valid mecahnisms");
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
advt_addr6(struct in6_addr* addr6, int prefix_len)
{
	/* First, we need to find a proper device to assign the address */
	char*	if_name = get_if(addr6, &prefix_len);
	int	i;
	if (NULL == if_name) {
		cl_log(LOG_ERR, "no valid mecahnisms");
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
stop_addr6(struct in6_addr* addr6, int prefix_len)
{
	char* if_name;
	if(OCF_NOT_RUNNING == status_addr6(addr6,prefix_len)) {
		return OCF_SUCCESS;
	}

	if_name = get_if(addr6, &prefix_len);

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
status_addr6(struct in6_addr* addr6, int prefix_len)
{
	char* if_name = get_if(addr6, &prefix_len);
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
 * Please refer to rfc2461
 */
int
send_ua(struct in6_addr* src_ip, char* if_name)
{
	libnet_t *l;
	char errbuf[LIBNET_ERRBUF_SIZE];

	struct libnet_in6_addr dst_ip;
	struct libnet_ether_addr *mac_address;
	char payload[24];


	if ((l=libnet_init(LIBNET_RAW6, if_name, errbuf)) == NULL) {
		cl_log(LOG_ERR, "libnet_init failure on %s", if_name);
		return -1;
	}

	mac_address = libnet_get_hwaddr(l);
	if (!mac_address) {
		cl_log(LOG_ERR, "libnet_get_hwaddr: %s", errbuf);
		return -1;
	}

	dst_ip = libnet_name2addr6(l, BCAST_ADDR, LIBNET_DONT_RESOLVE);

	memcpy(payload,src_ip->s6_addr,16);
	payload[16] = 2; /* 2 for Target Link-layer Address */
	payload[17] = 1; /* The length of the option */
	memcpy(payload+18,mac_address->ether_addr_octet, 6);

	libnet_seed_prand(l);
	/* 0x2000: RSO */
	libnet_build_icmpv4_echo(136,0,0,0x2000,0,(u_int8_t *)payload
			,sizeof(payload), l, LIBNET_PTAG_INITIALIZER);
	libnet_build_ipv6(0,0,LIBNET_ICMPV6_H + sizeof(payload),IPPROTO_ICMP6,
				255,*(struct libnet_in6_addr*)src_ip,
				dst_ip,NULL,0,l,0);

        if (libnet_write(l) == -1)
        {
		cl_log(LOG_ERR, "libnet_write: %s", libnet_geterror(l));
		return -1;
	}

	return 0;
}

/* find a proper network interface to assign the address */
char*
find_if(struct in6_addr* addr_target, int* plen_target)
{
	FILE *f;
	char addr6[40];
	static char devname[20]="";
	struct in6_addr addr;
	struct in6_addr mask;
	unsigned int plen, scope, dad_status, if_idx;
	char addr6p[8][5];

	/* open /proc/net/if_inet6 file */
	if ((f = fopen(IF_INET6, "r")) == NULL) {
		return NULL;
	}

	/* Loop for each entry */
	while ( fscanf(f,"%4s%4s%4s%4s%4s%4s%4s%4s %02x %02x %02x %02x %20s\n",
			addr6p[0], addr6p[1], addr6p[2], addr6p[3],
			addr6p[4], addr6p[5], addr6p[6], addr6p[7],
			&if_idx, &plen, &scope, &dad_status, devname) != EOF){

		int		i;
		int		n;
		int		s;
		gboolean	same = TRUE;

		sprintf(addr6, "%s:%s:%s:%s:%s:%s:%s:%s",
			addr6p[0], addr6p[1], addr6p[2], addr6p[3],
			addr6p[4], addr6p[5], addr6p[6], addr6p[7]);
	
		/* Only Global address entry would be considered.
		 * maybe change?
		 */
		if (0 != scope) {
			continue;
		}

		/* If specified prefix, only same prefix entry
		 * would be considered.
		 */
		if (*plen_target!=0 && plen != *plen_target) {
			continue;
		}
		*plen_target = plen;
		
		/* Convert string to sockaddr_in6 */
		inet_pton(AF_INET6, addr6, &addr);

		/* Make the mask based on prefix length */
		for (i = 0; i < 16; i++) {
			mask.s6_addr[i] = 0;
		}	
		n = plen / 8;
		for (i = 0; i < n+1; i++) {
			mask.s6_addr[i] = 0xFF;
		}
		s = 8 - plen % 8;
		mask.s6_addr[n]<<=s;

		/* compare addr and addr_target */
		same = TRUE;
		for (i = 0; i < 16; i++) {
			if ((addr.s6_addr[i]&mask.s6_addr[i]) !=
			    (addr_target->s6_addr[i]&mask.s6_addr[i])) {
				same = FALSE;
				break;
			}
		}
		
		/* We found it!	*/
		if (same) {
			fclose(f);
			return devname;
		}
	}
	fclose(f);
	return NULL;
}
/* get the device name and the plen_target of a special address */
char*
get_if(struct in6_addr* addr_target, int* plen_target)
{
	FILE *f;
	char addr6[40];
	static char devname[20]="";
	struct in6_addr addr;
	unsigned int plen, scope, dad_status, if_idx;
	char addr6p[8][5];

	/* open /proc/net/if_inet6 file */
	if ((f = fopen(IF_INET6, "r")) == NULL) {
		return NULL;
	}
	/* loop for each entry */
	while ( fscanf(f,"%4s%4s%4s%4s%4s%4s%4s%4s %02x %02x %02x %02x %20s\n",
		addr6p[0], addr6p[1], addr6p[2], addr6p[3],
		addr6p[4], addr6p[5], addr6p[6], addr6p[7],
		&if_idx, &plen, &scope, &dad_status, devname) != EOF) {

		sprintf(addr6, "%s:%s:%s:%s:%s:%s:%s:%s",
			addr6p[0], addr6p[1], addr6p[2], addr6p[3],
			addr6p[4], addr6p[5], addr6p[6], addr6p[7]);

		/* Only Global address entry would be considered.
		 * maybe change
		 */
		if (0 != scope) {
			continue;
		}

		/* "if" specified prefix, only same prefix entry
		 * would be considered.
		 */
		if (*plen_target!=0 && plen != *plen_target) {
			continue;
		}
		*plen_target = plen;

		/* Convert to sockaddr_in6 */
		inet_pton(AF_INET6, addr6, &addr);

		/* We found it! */
		if (0 == memcmp(&addr, addr_target,sizeof(addr))) {
			fclose(f);
			return devname;
		}
	}
	fclose(f);
	return NULL;
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
	struct libnet_icmpv6_hdr	icmph;
	u_char				outpack[MINPACKSIZE];
	int				icmp_sock;
	int				ret;
	struct iovec			iov;
	u_char				packet[MINPACKSIZE];
	struct msghdr			msg;

	icmp_sock = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
	memset(&icmph, 0, sizeof(icmph));
	icmph.icmp_type = ICMP6_ECHO;
	icmph.icmp_code = 0;
	icmph.icmp_sum = 0;
	icmph.seq = htons(0);
	icmph.id = 0;

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
	printf("usage: %s ipv6-address {start|stop|status|monitor}\n",self);
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
		free(dir);
		return -1;
	}

	if (!status) {
		if (S_ISDIR(stat_buf.st_mode)) {
			return 0;
		}
		cl_log(LOG_INFO, "Pid-File directory exists but is "
				"not a directory [%s]", dir);
		free(dir);
		return -1;
        }

	if (mkdir(dir, S_IRUSR|S_IWUSR|S_IXUSR | S_IRGRP|S_IXGRP) < 0) {
		cl_log(LOG_INFO, "Could not create pid-file directory "
				"[%s]: %s", dir, strerror(errno));
		free(dir);
		return -1;
	}

	return 0;
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
	"<resource-agent name=\"IPv6addr\" version=\"0.1\">\n"
	"  <version>1.0</version>\n"
	"  <parameters>\n"
	"    <parameter name=\"ipv6addr\" unique=\"0\">\n"
	"      <longdesc lang=\"en\">\n"
	"        This script manages IPv6 alias IPv6 addresses,It can add an IP6\n"
	"        alias, or remove one.\n"
	"      </longdesc>\n"
	"      <shortdesc lang=\"en\">manages IPv6 alias</shortdesc>\n"
	"      <content type=\"string\" default=\"\" />\n"
	"    </parameter>\n"
	"  </parameters>\n"
	"  <actions>\n"
	"    <action name=\"start\"   timeout=\"15\" />\n"
	"    <action name=\"stop\"    timeout=\"15\" />\n"
	"    <action name=\"status\"  timeout=\"15\" interval=\"15\" start-delay=\"15\" />\n"
	"    <action name=\"monitor\" timeout=\"15\" interval=\"15\" start-delay=\"15\" />\n"
	"    <action name=\"meta-data\"  timeout=\"5\" />\n"
	"  </actions>\n"
	"</resource-agent>\n";
	printf("%s\n",meta_data);
	return 0;
}
