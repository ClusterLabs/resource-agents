/*
 * findif.c:	Finds an interface which can route a given address
 *
 *	It's really simple to write in C, but hard to write in the shell...
 *
 *	This code is dependent on IPV4 addressing conventions...
 *		Sorry.
 *
 * Copyright (C) 2000 Alan Robertson <alanr@unix.sh>
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 *
 ***********************************************************
 *
 *	All our arguments come through the environment as OCF
 *	environment variables as below:
 *
 *	OCF_RESKEY_ip
 *	OCF_RESKEY_broadcast
 *	OCF_RESKEY_nic
 *	OCF_RESKEY_cidr_netmask
 *
 *	If the CIDR netmask is omitted, we choose the netmask associated with
 *	the route we selected.
 *
 *	If the broadcast address was omitted, we assume the highest address
 *	in the subnet.
 *
 *	If the interface is omitted, we choose the interface associated with
 *	the route we selected.
 *
 *
 *	See http://www.doom.net/docs/netmask.html for a table explaining
 *	CIDR address format and their relationship to life, the universe
 *	and everything.
 *
 */

#include <config.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif
#include <net/if.h>
#include <sys/ioctl.h>
#include <errno.h>
#ifdef __linux__
#undef __OPTIMIZE__
/*
 * This gets rid of some silly -Wtraditional warnings on Linux
 * because the netinet header has some slightly funky constants
 * in it.
 */
#endif /* __linux__ */

#include <netinet/in.h>
#include <arpa/inet.h>
#include <agent_config.h>
#include <config.h>

#define DEBUG 0
#define	EOS			'\0'
#define	PROCROUTE	"/proc/net/route"
#define ROUTEPARM	"-n get"

#ifndef HAVE_STRNLEN
/* Any system that don't provide strnlen() only has itself to blame */
#define strnlen(str, max) strlen(str)
#endif

/*
 * "route -n get iii.jjj.kkk.lll" can, on Solaris at least,
 * return the word "default" as the value from "mask" and "dest",
 * typically if the host is remote, reached over a default route. 
 * We should probably treat such a mask as "0.0.0.0".
 *
 * Define "MASK_DEFAULT_TO_ZERO" to enable this interpretation.
 *
 * This is better for Solaris and is probably suitable (or irrelevant)
 * for others OSes also.  But if it breaks another OS, then reduce the
 * "hash-if 1" below to exclude that OS. 
 * (David Lee, Jan 2006)
 */
#if 1
# define MASK_DEFAULT_TO_ZERO
#endif

static int OutputInCIDR=0;


/*
 * Different OSes offer different mechnisms to obtain this information.
 * Not all this can be determined at configure-time; need a run-time element.
 *
 * typedef ... SearchRoute ...:
 *	For routines that interface on these mechanisms.
 *	Return code:
 *		<0:	mechanism invalid, so try next mechanism
 *		0:	mechanism worked: good answer
 *		>0:	mechanism worked: bad answer
 *	On non-zero, errmsg may have been filled with an error message
 */
typedef int SearchRoute (char *address, struct in_addr *in
,        struct in_addr *addr_out, char *best_if, size_t best_iflen
,        unsigned long *best_netmask, char *errmsg
,	int errmsglen);

static SearchRoute SearchUsingProcRoute;
static SearchRoute SearchUsingRouteCmd;

static SearchRoute *search_mechs[] = {
	&SearchUsingProcRoute,
	&SearchUsingRouteCmd,
	NULL
};

void GetAddress (char **address, char **netmaskbits
,	 char **bcast_arg, char **if_specified);

int ConvertNetmaskBitsToInt(char *netmaskbits);

void ValidateNetmaskBits(int bits, unsigned long *netmask);

int ValidateIFName (const char *ifname, struct ifreq *ifr);

int netmask_bits (unsigned long netmask);

char * get_first_loopback_netdev(char * ifname);
int is_loopback_interface(char * ifname);
char * get_ifname(char * buf, char * ifname);

int ConvertQuadToInt(char *dest);

static const char *cmdname = "findif";
#define OCF_SUCCESS             0
#define OCF_ERR_GENERIC         1
#define OCF_ERR_ARGS            2
#define OCF_ERR_UNIMPLEMENTED   3
#define OCF_ERR_PERM            4
#define OCF_ERR_INSTALLED       5
#define OCF_ERR_CONFIGURED      6
#define OCF_NOT_RUNNING         7
void usage(int ec);

#define PATH_PROC_NET_DEV "/proc/net/dev"
#define DELIM	'/'
#define	BAD_BROADCAST	(0L)
#define	MAXSTR	128

static int
SearchUsingProcRoute (char *address, struct in_addr *in
, 	struct in_addr *addr_out, char *best_if, size_t best_iflen
,	unsigned long *best_netmask
,	char *errmsg, int errmsglen)
{
	unsigned long	flags, refcnt, use, gw, mask;
	unsigned long   dest;
	long		metric = LONG_MAX;
	long		best_metric = LONG_MAX;
	int		rc = OCF_SUCCESS;
	
	char	buf[2048];
	char	interface[MAXSTR];
	FILE *routefd = NULL;

	if ((routefd = fopen(PROCROUTE, "r")) == NULL) {
		snprintf(errmsg, errmsglen
		,	"Cannot open %s for reading"
		,	PROCROUTE);
		rc = OCF_ERR_GENERIC; goto out;
	}

	/* Skip first (header) line */
	if (fgets(buf, sizeof(buf), routefd) == NULL) {
		snprintf(errmsg, errmsglen
		,	"Cannot skip first line from %s"
		,	PROCROUTE);
		rc = OCF_ERR_GENERIC; goto out;
	}
	*best_netmask = 0;
	while (fgets(buf, sizeof(buf), routefd) != NULL) {
		if (sscanf(buf, "%[^\t]\t%lx%lx%lx%lx%lx%lx%lx"
		,	interface, &dest, &gw, &flags, &refcnt, &use
		,	&metric, &mask)
		!= 8) {
			snprintf(errmsg, errmsglen, "Bad line in %s: %s"
			,	PROCROUTE, buf);
			rc = OCF_ERR_GENERIC; goto out;
		}
		if ( (in->s_addr&mask) == (in_addr_t)(dest&mask)
		&&	metric <= best_metric && mask >= *best_netmask) {
			best_metric = metric;
			*best_netmask = mask;
			strncpy(best_if, interface, best_iflen);
		}
	}

	if (best_metric == LONG_MAX) {
		snprintf(errmsg, errmsglen, "No route to %s\n", address);
		rc = OCF_ERR_GENERIC; 
	}

  out:
	if (routefd) {
		fclose(routefd);
	}

	return(rc);
}

static int
SearchUsingRouteCmd (char *address, struct in_addr *in
,	struct in_addr *addr_out, char *best_if, size_t best_iflen
,	unsigned long *best_netmask
,	char *errmsg, int errmsglen)
{
	char	mask[20];
	char	routecmd[MAXSTR];
	int	best_metric = INT_MAX;	
	char	buf[2048];
	char	interface[MAXSTR];
	char  *cp, *sp;
	int done = 0;
	FILE *routefd = NULL;
	uint32_t maskbits;

	
	/* Open route and get the information */
	snprintf (routecmd, sizeof(routecmd), "%s %s %s"
	,	ROUTE, ROUTEPARM, address);
	routefd = popen (routecmd, "r");
	if (routefd == NULL)
		return (OCF_ERR_GENERIC);
	mask[0] = EOS;
	interface[0] = EOS;


	while ((done < 3) && fgets(buf, sizeof(buf), routefd)) {
		int buflen = strnlen(buf, sizeof(buf));
		/*cp = buf;*/

		sp = buf + buflen;
		while (sp!=buf && isspace((int)*(sp-1))) {
			--sp;
		}
		*sp = EOS;

		if (strstr (buf, "mask:")) {
			/*strsep(&cp, ":");cp++;*/
			strtok(buf, ":");
			cp = strtok(NULL, ":");
			if (cp) {
				cp++;
				strncpy(mask, cp, sizeof(mask));
				done++;
			}
		}

		if (strstr (buf, "interface:")) {
			/*strsep(&cp, ":");cp++;*/
			strtok(buf, ":");
			cp = strtok(NULL, ":");
			if (cp) {
				cp++;
				strncpy(interface, cp, sizeof(interface));
				done++;
			}
		}
	}
	fclose(routefd);

	/*
	 * Check to see if mask isn't available.  It may not be
	 *	returned if multiple IP's are defined.
	 *	use 255.255.255.255 for mask then
	 */
	/* I'm pretty sure this is the wrong behavior...
	 * I think the right behavior is to declare an error and give up.
	 * The admin didn't define his routes correctly.  Fix them.
	 * It's useless to take over an IP address with no way to
	 * return packets to the originator.  Without the right subnet
	 * mask, you can't reply to any packets you receive.
	 */
	if (strnlen(mask, sizeof(mask)) == 0) {
		strncpy (mask, "255.255.255.255", sizeof(mask));
	}

	/*
	 * Solaris (at least) can return the word "default" for mask and dest.
	 * For the moment, let's interpret this as:
	 *	mask: 0.0.0.0
	 * This was manifesting itself under "BasicSanityCheck", which tries
	 * to use a remote IP number; these typically use the "default" route.
	 * Better schemes are warmly invited...
	 */
#ifdef MASK_DEFAULT_TO_ZERO
	if (strncmp(mask, "default", sizeof("default")) == 0) {
		strncpy (mask, "0.0.0.0", sizeof(mask));
	}
#endif

	if (inet_pton(AF_INET, mask, &maskbits) <= 0) {
		snprintf(errmsg, errmsglen,
		  "mask [%s] not valid.", mask);
		return(OCF_ERR_CONFIGURED);
	}

	if (inet_pton(AF_INET, address, addr_out) <= 0) {
		snprintf(errmsg, errmsglen
		,	"IP address [%s] not valid.", address);
		return(OCF_ERR_CONFIGURED);
	}

	if ((in->s_addr & maskbits) == (addr_out->s_addr & maskbits)) {
		if (interface[0] == EOS) {
			snprintf(errmsg, errmsglen, "No interface found.");
			return(OCF_ERR_GENERIC);
		}
		best_metric = 0;
		*best_netmask = maskbits;
		strncpy(best_if, interface, best_iflen);
	}

	if (best_metric == INT_MAX) {
		snprintf(errmsg, errmsglen, "No route to %s\n", address);
		return(OCF_ERR_GENERIC);
	}

	return (OCF_SUCCESS);
}

/*
 * Getaddress gets all its real parameters from the OCF environment
 * variables that its callers already use.
 */
void
GetAddress (char **address, char **netmaskbits
,	 char **bcast_arg, char **if_specified)
{
	/*
	 *	Here are out input environment variables:
	 *
	 *	OCF_RESKEY_ip		ip address
	 *	OCF_RESKEY_cidr_netmask netmask of interface
	 *	OCF_RESKEY_broadcast	broadcast address for interface
	 *	OCF_RESKEY_nic		interface to assign to
	 *
	 */
	*address = getenv("OCF_RESKEY_ip");
	*netmaskbits = getenv("OCF_RESKEY_cidr_netmask");
	if (*netmaskbits == NULL || **netmaskbits == EOS) {
		*netmaskbits = getenv("OCF_RESKEY_netmask");
	}
	*bcast_arg = getenv("OCF_RESKEY_broadcast");
	*if_specified = getenv("OCF_RESKEY_nic");
}

int
ConvertNetmaskBitsToInt(char *netmaskbits)
{
	size_t	nmblen = strnlen(netmaskbits, 3);

	/* Maximum netmask is 32 */

	if (nmblen > 2 || nmblen == 0
	||	(strspn(netmaskbits, "0123456789") != nmblen))
		return -1;
	else
		return atoi(netmaskbits);
}

void
ValidateNetmaskBits(int bits, unsigned long *netmask)
{
	/* Maximum netmask is 32 */

	if (bits < 1 || bits > 32) {
		fprintf(stderr
		,	"Invalid netmask specification [%d]"
		,	bits);
		usage(OCF_ERR_CONFIGURED);
		/*not reached */
	}

	bits = 32 - bits;
	*netmask = (1L<<(bits))-1L;
	*netmask = ((~(*netmask))&0xffffffffUL);
	*netmask = htonl(*netmask);
}

int
ValidateIFName(const char *ifname, struct ifreq *ifr) 
{
 	int skfd = -1;
	char *colonptr;

 	if ( (skfd = socket(PF_INET, SOCK_DGRAM, 0)) == -1 ) {
 		fprintf(stderr, "%s\n", strerror(errno));
 		return -2;
 	}
 
	strncpy(ifr->ifr_name, ifname, IFNAMSIZ);

	/* Contain a ":"?  Probably an error, but treat as warning at present */
	if ((colonptr = strchr(ifname, ':')) != NULL) {
		fprintf(stderr, "%s: warning: name may be invalid\n",
		  ifr->ifr_name);
	}
 
 	if (ioctl(skfd, SIOCGIFFLAGS, ifr) < 0) {
 		fprintf(stderr, "%s: unknown interface: %s\n"
 			, ifr->ifr_name, strerror(errno));
 		close(skfd);
		/* return -1 only if ifname is known to be invalid */
		return -1;
 	}
 	close(skfd);
 	return 0;
} 

int
netmask_bits(unsigned long netmask)
{
	int	j;

	netmask = netmask & 0xFFFFFFFFUL;

	for (j=0; j <= 32; ++j) {
		if ((netmask >> j)&0x1) {
			break;
		}
	}

	return 32 - j;
}

char * 
get_first_loopback_netdev(char * output)
{
	char buf[512];
	FILE * fd = NULL;
	char *rc = NULL;
	
	if (!output) {
		fprintf(stderr, "output buf is a null pointer.\n");
		goto out;
	}

	fd = fopen(PATH_PROC_NET_DEV, "r");
	if (!fd) {
		fprintf(stderr, "Warning: cannot open %s (%s).\n",
			PATH_PROC_NET_DEV, strerror(errno)); 
		goto out;
	}

	/* Skip the first two lines */
	if (!fgets(buf, sizeof(buf), fd) || !fgets(buf, sizeof(buf), fd)) {
		fprintf(stderr, "Warning: cannot read header from %s.\n",
			PATH_PROC_NET_DEV);
		goto out;
	}

	while (fgets(buf, sizeof(buf), fd)) {
		char name[IFNAMSIZ];
		if (NULL == get_ifname(buf, name)) {
			/* Maybe somethin is wrong, anyway continue */
			continue;
		}
		if (is_loopback_interface(name)) {
			strncpy(output, name, IFNAMSIZ);
			rc = output;
			goto out;
		}
	}

out:
	if (fd) {
		fclose(fd);
	}
	return rc;
}

int
is_loopback_interface(char * ifname)
{
	struct ifreq ifr;
	memset(&ifr, 0, sizeof(ifr));
	if (ValidateIFName(ifname, &ifr) < 0)
		return 0;

	if (ifr.ifr_flags & IFF_LOOPBACK) {
		/* this is a loopback device. */
		return 1;
	} else {
		return 0;
	}
}

char *
get_ifname(char * buf, char * ifname)
{
	char * start, * end, * buf_border;	

	buf_border = buf + strnlen(buf, 512);

	start = buf; 
	while (isspace((int) *start) && (start != buf_border)) {
		start++;
	}
	end = start;
	while ((*end != ':') && (end != buf_border)) {
		end++;	
	}

	if ( start == buf_border || end == buf_border ) {
		/* Over the border of buf */ 
		return NULL;
	}

	*end = '\0';
	strncpy(ifname, start, IFNAMSIZ);
	
	return ifname;
}

int ConvertQuadToInt(char *dest)
{
	struct in_addr ad;

	if (inet_pton(AF_INET, dest, &ad) <= 0)
		return -1;

	return netmask_bits(ntohl(ad.s_addr));
}

int
main(int argc, char ** argv) {

	char *	address = NULL;
	char *	bcast_arg = NULL;
	char *	netmaskbits = NULL;
	struct in_addr	in;
	struct in_addr	addr_out;
	unsigned long	netmask;
	char	best_if[MAXSTR];
	char *	if_specified = NULL;
	struct ifreq	ifr;
	unsigned long	best_netmask = INT_MAX;
	int		argerrs	= 0;
	int		nmbits;

	cmdname=argv[0];


	memset(&addr_out, 0, sizeof(addr_out));
	memset(&in, 0, sizeof(in));
	memset(&ifr, 0, sizeof(ifr));

	switch (argc) {
	case 1:	/* No -C argument */
		break;
	case 2: /* Hopefully a -C argument */
		if (strncmp(argv[1], "-C", sizeof("-C")) != 0) {
			argerrs=1;
		}
		OutputInCIDR=1;
		break;
	default:
		argerrs=1;
		break;
	}
	if (argerrs) {
		usage(OCF_ERR_ARGS);
		/* not reached */
		return(1);
	}

	GetAddress (&address, &netmaskbits, &bcast_arg
	,	 &if_specified);
	if (address == NULL || *address == EOS) {
		fprintf(stderr, "ERROR: IP address parameter is mandatory.");
		usage(OCF_ERR_CONFIGURED);
		/* not reached */
	}

	/* Is the IP address we're supposed to find valid? */
	 
	if (inet_pton(AF_INET, address, (void *)&in) <= 0) {
		fprintf(stderr, "IP address [%s] not valid.", address);
		usage(OCF_ERR_CONFIGURED);
		/* not reached */
	}

	if (netmaskbits != NULL && *netmaskbits != EOS) {
		if (strchr(netmaskbits, '.') != NULL) {
			nmbits = ConvertQuadToInt(netmaskbits);
			fprintf(stderr, "Converted dotted-quad netmask to CIDR as: %d\n", nmbits);
		}else{
			nmbits = ConvertNetmaskBitsToInt(netmaskbits);
		}

		if (nmbits < 0) {
			fprintf(stderr, "Invalid netmask specification"
			" [%s]", netmaskbits);
			usage(OCF_ERR_CONFIGURED);
			/*not reached */
		}

		/* Validate the netmaskbits field */
		ValidateNetmaskBits (nmbits, &netmask);
	}


	if (if_specified != NULL && *if_specified != EOS) {
		if(ValidateIFName(if_specified, &ifr) < 0) {
			usage(OCF_ERR_CONFIGURED);
			/* not reached */
		}
		strncpy(best_if, if_specified, sizeof(best_if));
		*(best_if + sizeof(best_if) - 1) = '\0';
	}else{
		SearchRoute **sr = search_mechs;
		char errmsg[MAXSTR] = "No valid mechanisms";
		int rc = OCF_ERR_GENERIC;

		strcpy(best_if, "UNKNOWN");

		while (*sr) {
			errmsg[0] = '\0';
			rc = (*sr) (address, &in, &addr_out, best_if
			,	sizeof(best_if)
			,	&best_netmask, errmsg, sizeof(errmsg));
			if (!rc) {		/* Mechanism worked */
				break;
			}
			sr++;
		}
		if (rc != 0) {	/* No route, or all mechanisms failed */
			if (*errmsg) {
				fprintf(stderr, "%s", errmsg);
			}
			return(rc);
		}
	}

	if (netmaskbits) {
		best_netmask = netmask;
	}else if (best_netmask == 0L) {
		/*
		   On some distributions, there is no loopback related route
		   item, this leads to the error here.
		   My fix may be not good enough, please FIXME
		 */
		if (0 == strncmp(address, "127", 3)) {
			if (NULL != get_first_loopback_netdev(best_if)) {
				best_netmask = 0x000000ff;
			} else {
				fprintf(stderr, "No loopback interface found.\n");
				return(OCF_ERR_GENERIC);
			}
		} else {
			fprintf(stderr
			,	"ERROR: Cannot use default route w/o netmask [%s]\n"
			,	 address);
			return(OCF_ERR_GENERIC);
		}
	}

	/* Did they tell us the broadcast address? */

	if (bcast_arg && *bcast_arg != EOS) {
		/* Yes, they gave us a broadcast address.
		 * It at least should be a valid IP address
		 */
 		struct in_addr bcast_addr;
 		if (inet_pton(AF_INET, bcast_arg, (void *)&bcast_addr) <= 0) {
 			fprintf(stderr, "Invalid broadcast address [%s].", bcast_arg);
 			usage(OCF_ERR_CONFIGURED);
			/* not reached */
 		}

		best_netmask = htonl(best_netmask);
		if (!OutputInCIDR) {
			printf("%s\tnetmask %d.%d.%d.%d\tbroadcast %s\n"
			,	best_if
                	,       (int)((best_netmask>>24) & 0xff)
                	,       (int)((best_netmask>>16) & 0xff)
                	,       (int)((best_netmask>>8) & 0xff)
                	,       (int)(best_netmask & 0xff)
			,	bcast_arg);
		}else{
			printf("%s\tnetmask %d\tbroadcast %s\n"
			,	best_if
			,	netmask_bits(best_netmask)
			,	bcast_arg);
		}
	}else{
		/* No, we use a common broadcast address convention */
		unsigned long	def_bcast;

			/* Common broadcast address */
		def_bcast = (in.s_addr | (~best_netmask));
#if DEBUG
		fprintf(stderr, "best_netmask = %08lx, def_bcast = %08lx\n"
		,	best_netmask,  def_bcast);
#endif

		/* Make things a bit more machine-independent */
		best_netmask = htonl(best_netmask);
		def_bcast = htonl(def_bcast);
		if (!OutputInCIDR) {
			printf("%s\tnetmask %d.%d.%d.%d\tbroadcast %d.%d.%d.%d\n"
			,       best_if
			,       (int)((best_netmask>>24) & 0xff)
			,       (int)((best_netmask>>16) & 0xff)
			,       (int)((best_netmask>>8) & 0xff)
			,       (int)(best_netmask & 0xff)
			,       (int)((def_bcast>>24) & 0xff)
			,       (int)((def_bcast>>16) & 0xff)
			,       (int)((def_bcast>>8) & 0xff)
			,       (int)(def_bcast & 0xff));
		}else{
			printf("%s\tnetmask %d\tbroadcast %d.%d.%d.%d\n"
			,       best_if
			,	netmask_bits(best_netmask)
			,       (int)((def_bcast>>24) & 0xff)
			,       (int)((def_bcast>>16) & 0xff)
			,       (int)((def_bcast>>8) & 0xff)
			,       (int)(def_bcast & 0xff));
		}
	}
	return(0);
}

void
usage(int ec)
{
	fprintf(stderr, "\n"
		"%s version 2.99.1 Copyright Alan Robertson\n"
		"\n"
		"Usage: %s [-C]\n"
		"Options:\n"
		"    -C: Output netmask as the number of bits rather "
			"than as 4 octets.\n"
		"Environment variables:\n"
		"OCF_RESKEY_ip		 ip address (mandatory!)\n"
		"OCF_RESKEY_cidr_netmask netmask of interface\n"
		"OCF_RESKEY_broadcast	 broadcast address for interface\n"
		"OCF_RESKEY_nic		 interface to assign to\n"
	,	cmdname, cmdname);
	exit(ec);
}

/*
Iface	Destination	Gateway 	Flags	RefCnt	Use	Metric	Mask		MTU	Window	IRTT                                                       
eth0	33D60987	00000000	0005	0	0	0	FFFFFFFF	0	0	0                                                                               
eth0	00D60987	00000000	0001	0	0	0	00FFFFFF	0	0	0                                                                               
lo	0000007F	00000000	0001	0	0	0	000000FF	0	0	0                                                                                 
eth0	00000000	FED60987	0003	0	0	0	00000000	0	0	0                                                                               

netstat -rn outpug from RedHat Linux 6.0
Kernel IP routing table
Destination     Gateway         Genmask         Flags   MSS Window  irtt Iface
192.168.85.2    0.0.0.0         255.255.255.255 UH        0 0          0 eth1
10.0.0.2        0.0.0.0         255.255.255.255 UH        0 0          0 eth2
208.132.134.61  0.0.0.0         255.255.255.255 UH        0 0          0 eth0
208.132.134.32  0.0.0.0         255.255.255.224 U         0 0          0 eth0
192.168.85.0    0.0.0.0         255.255.255.0   U         0 0          0 eth1
10.0.0.0        0.0.0.0         255.255.255.0   U         0 0          0 eth2
127.0.0.0       0.0.0.0         255.0.0.0       U         0 0          0 lo
0.0.0.0         208.132.134.33  0.0.0.0         UG        0 0          0 eth0

|--------------------------------------------------------------------------------
netstat -rn output from FreeBSD 3.3
Routing tables

Internet:
Destination        Gateway            Flags     Refs     Use     Netif Expire
default            209.61.94.161      UGSc        3        8      pn0
192.168            link#1             UC          0        0      xl0
192.168.0.2        0:60:8:a4:91:fd    UHLW        0       38      lo0
192.168.0.255      ff:ff:ff:ff:ff:ff  UHLWb       1     7877      xl0
209.61.94.160/29   link#2             UC          0        0      pn0
209.61.94.161      0:a0:cc:26:c2:ea   UHLW        6    17265      pn0   1105
209.61.94.162      0:a0:cc:27:1c:fb   UHLW        1      568      pn0   1098
209.61.94.163      0:a0:cc:29:1f:86   UHLW        0     4749      pn0   1095
209.61.94.166      0:a0:cc:27:2d:e1   UHLW        0       12      lo0
209.61.94.167      ff:ff:ff:ff:ff:ff  UHLWb       0    10578      pn0

|--------------------------------------------------------------------------------
netstat -rn output from FreeBSD 4.2
Routing tables

Internet:
Destination        Gateway            Flags     Refs     Use     Netif Expire
default            64.65.195.1        UGSc        1       11      dc0
64.65.195/24       link#1             UC          0        0      dc0 =>
64.65.195.1        0:3:42:3b:0:dd     UHLW        2        0      dc0   1131
64.65.195.184      0:a0:cc:29:1f:86   UHLW        2    18098      dc0   1119
64.65.195.194      0:a0:cc:27:2d:e1   UHLW        3   335161      dc0    943
64.65.195.200      52:54:0:db:33:b3   UHLW        0       13      dc0    406
64.65.195.255      ff:ff:ff:ff:ff:ff  UHLWb       1      584      dc0
127.0.0.1          127.0.0.1          UH          0        0      lo0
192.168/16         link#2             UC          0        0      vx0 =>
192.168.0.1        0:20:af:e2:f0:36   UHLW        0        2      lo0
192.168.255.255    ff:ff:ff:ff:ff:ff  UHLWb       0        1      vx0

Internet6:
Destination                       Gateway                       Flags      Netif Expire
::1                               ::1                           UH          lo0
fe80::%dc0/64                     link#1                        UC          dc0
fe80::%vx0/64                     link#2                        UC          vx0
fe80::%lo0/64                     fe80::1%lo0                   Uc          lo0
ff01::/32                         ::1                           U           lo0
ff02::%dc0/32                     link#1                        UC          dc0
ff02::%vx0/32                     link#2                        UC          vx0
ff02::%lo0/32                     fe80::1%lo0                   UC          lo0
*/

