/*
  Copyright Red Hat, Inc. 2004,2006

  The Magma Cluster API Library is free software; you can redistribute
  it and/or modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either version
  2.1 of the License, or (at your option) any later version.

  The Magma Cluster API Library is distributed in the hope that it will
  be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
  of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
*/
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
