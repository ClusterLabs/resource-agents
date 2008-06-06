/** @file
 * Header for ip_lookup.c
 */
#ifndef _IP_LOOKUP_H
#define _IP_LOOKUP_H

#include <sys/queue.h>

typedef struct _ip_address {
	TAILQ_ENTRY(_ip_address) ipa_entries;
	char ipa_family;
	char *ipa_address;
} ip_addr_t;

typedef TAILQ_HEAD(_ip_list, _ip_address) ip_list_t;

int ip_search(ip_list_t *ipl, char *ip_name);
int ip_free_list(ip_list_t *ipl);
int ip_build_list(ip_list_t *ipl);
int ip_lookup(char *, struct addrinfo **);

#endif
