/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  2002-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/
#ifndef __utils_ip_h__
#define __utils_ip_h__
#include "LLi.h"
typedef struct {
   LLi_t in_list;
   struct in6_addr ip;
   uint8_t *name;
} ip_name_t;

int get_ip_from_netdev(char *name, struct in6_addr *ip6);
int get_name_for_ip(char *name, size_t nlen, uint32_t ip);
int get_ip_for_name(char *name, struct in6_addr *ip);

__inline__ char *iptostr(uint32_t ip);
__inline__ const char *ip6tostr(struct in6_addr *ip);

ip_name_t *get_ipname(char *str);
const char *print_ipname(ip_name_t *in);

#endif /*__utils_ip_h__*/
/* vim: set ai cin et sw=3 ts=3 : */
