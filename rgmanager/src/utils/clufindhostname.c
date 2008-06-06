/** @file
 * Utility/command to return name found by gethostbyname and gethostbyaddr.
 *
 * Author: Richard Rabbat <rabbat@missioncriticallinux.com>
 * IPv6 support added 7/2006
 */
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>

void
usage(char *progname)
{
	fprintf(stderr, "Usage: %s [-i ip_addr] [-n ip_name]\n", progname);
}

int
main(int argc, char **argv)
{
	struct hostent *hp;
	void *ptr;
	struct in_addr  addr4;
	struct in6_addr addr6;
	int opt, size, family;
	char *sep;

	if (argc != 3) {
		usage(argv[0]);
		exit(1);
	}

	while ((opt = getopt(argc, argv, "i:n:")) != EOF) {
		switch (opt) {
		case 'i':
			/* Check for IPv4 address */
			sep = strchr(optarg, '.');
			if (sep) {
				family = AF_INET;
				ptr = &addr4;
				size = sizeof(addr4);
			} else {
				family = AF_INET6;
				ptr = &addr6;
				size = sizeof(addr6);
			}

			if (inet_pton(family, optarg, ptr) < 0) {
				perror("inet_pton");
				exit(2);
			}

			if (!(hp = gethostbyaddr(ptr, size, family))) {
				exit(2);
			} else {
				fprintf(stdout, "%s\n", hp->h_name);
				exit(0);
			}
			break;
		case 'n':
			if (!(hp = gethostbyname(argv[2]))) {
				exit(2);
			} else {
				fprintf(stdout, "%s\n", hp->h_name);
				exit(0);
			}
			break;
		default:
			break;
		}
	}
	exit(0);
}
