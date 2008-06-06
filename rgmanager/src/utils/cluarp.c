/** @file
 * Utility to generate a gratuitous ARP request on a given interface.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>

#define	IP_ADDR_LEN	4
#define	DEFAULT_DEVICE	"eth0"
#define SA_DATA_LEN     14   /* Taken from include/linux/socket.h */

/**
 * ARP frame structure.
 */
struct arp_frame {
	u_char	ether_dest_hw_addr[ETH_ALEN];
	u_char	ether_src_hw_addr[ETH_ALEN];
	u_short	ether_packet_type;
	u_short	arp_hw_type;
	u_short	arp_proto_type;
	u_char	arp_hlen;
	u_char	arp_plen;
	u_short	op;
	u_char	arp_sender_ha[ETH_ALEN];
	u_char	arp_sender_ip[IP_ADDR_LEN];
	u_char	arp_target_ha[ETH_ALEN];
	u_char	arp_target_ip[IP_ADDR_LEN];
	u_char	padding[18];
};


/**
 * Display usage information.
 */
void usage(void)
{
    fprintf(stdout, 
  "myarp <src_ip> <src_hw> <targ_ip> <targ_hw> [device]\n");
}


/**
 * Change a hardware ethernet address into an array of characters.
 * Warning: Unchecked bounds in copy_to.
 *
 * @param addr_string	NULL-terminated string in the format:
 *			"aa:bb:cc:dd:ee:ff"
 * @param copy_to	Pre-allocated array of at least 6 bytes,
 *			preferrably set to 0.
 */
void
parse_hw_addr(char *addr_string, u_char *copy_to)
{
        char	*c = addr_string, p, result=' ';  
	int	i;

	for (i = 0; i < ETH_ALEN; i++) {
	    /* first digit */
		if (*c == ':') 
			c++;
		p = tolower(*c++);	
		if (isdigit(p))
			result = p - '0'; 
		else if (p >= 'a' && p <= 'z')
			result = p - 'a' + 10;
		*copy_to = result << 4;
	    /* second digit */
		if (*c == ':') 
			c++;
		p = tolower(*c++);	
		if (isdigit(p))
			result = p - '0'; 
		else if (p >= 'a' && p <= 'z')
			result = p - 'a' + 10;
		*copy_to++ |= result;
	}
	return;
}


/**
 * Change an IP address into an array of characters.
 * Warning: Unchecked bounds in copy_to.
 *
 * @param addr_string	NULL-terminated string in the format:
 *			"10.1.2.3" (standard IPv4 dotted-quad).
 * @param copy_to	Pre-allocated array of at least 4 bytes,
 *			preferrably set to 0.
 */
void
parse_ip_addr(char *addr_string, u_char *copy_to)
{
	unsigned long		inaddr;

	inaddr = inet_addr(addr_string);
	memcpy(copy_to, &inaddr, IP_ADDR_LEN);

}


/**
 * Driver for cluarp.
 *
 * @return		0 on success, 1 on any failure.
 */
int
main(int argc, char **argv)
{    

	int	s;
	struct arp_frame	arp_packet;
	struct sockaddr		sockaddr;
/* 	unsigned long		inaddr; */

	if ((argc != 5) && (argc != 6)) {
		usage();	
		exit(1);
	}

	bzero(&arp_packet, sizeof(struct arp_frame));

	parse_ip_addr(argv[1], arp_packet.arp_sender_ip);
	parse_hw_addr(argv[2], arp_packet.ether_src_hw_addr);
	parse_hw_addr(argv[2], arp_packet.arp_sender_ha);
	parse_ip_addr(argv[3], arp_packet.arp_target_ip);
	parse_hw_addr(argv[4], arp_packet.ether_dest_hw_addr);
	parse_hw_addr(argv[4], arp_packet.arp_target_ha);

	arp_packet.ether_packet_type	= htons(ETH_P_ARP);
	arp_packet.arp_hw_type 		= htons(ETH_P_802_3);
	arp_packet.arp_proto_type	= htons(ETH_P_IP);
	arp_packet.arp_hlen		= ETH_ALEN;
	arp_packet.arp_plen		= IP_ADDR_LEN;
	arp_packet.op			= htons(ARPOP_REPLY);

	s = socket(AF_PACKET,SOCK_PACKET,htons(ETH_P_RARP));

	if (argc == 5)
		strncpy(sockaddr.sa_data, DEFAULT_DEVICE, SA_DATA_LEN);
	else
		strncpy(sockaddr.sa_data, argv[5], SA_DATA_LEN);

	if (sendto(s, &arp_packet, sizeof(struct arp_frame), 0,
		   &sockaddr, sizeof(struct sockaddr)) < 0) {
		perror("sendto");
		exit(1);
	}
	exit(0);
}


