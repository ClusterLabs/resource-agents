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
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include "utils_ip.h"

/**
 * map_v4_to_v6 - 
 * @ip4: 
 * @ip6: 
 * 
 */
void __inline__ map_v4_to_v6(struct in_addr *ip4, struct in6_addr *ip6)
{
   ip6->s6_addr32[0] = 0;
   ip6->s6_addr32[1] = 0;
   ip6->s6_addr32[2] = htonl(0xffff);
   ip6->s6_addr32[3] = ip4->s_addr;
}

/**
 * iptostr - print a dottedQuad from a 32bit int
 * @ip: < IP, in network byte order.
 * 
 * quick abstraction since inet_ntoa takes a slightly strange structure.
 * 
 * Returns: dotted Quad string
 */
__inline__ char *iptostr(uint32_t ip)
{
   struct in_addr in;
   in.s_addr = ip;
   return inet_ntoa(in);
}
__inline__ const char *ip6tostr(struct in6_addr *ip)
{
   static char t[80];
   return inet_ntop(AF_INET6, ip, t, 80);
}

/**
 * get_ip_from_netdev - 
 * @har: 
 * @ip6: 
 * 
 * 
 * Returns: int
 */
int get_ip_from_netdev(char *name, struct in6_addr *ip6)
{
   struct ifconf ifc;
   struct ifreq *ifr=NULL;
   int i, sock, err=0;
   int num_ifrs=20;
   ifc.ifc_buf = NULL;
   
   if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
      return -1;

   /* Borrowed from the ifconfig code */
   for(;;) {
      ifc.ifc_len = num_ifrs * sizeof(struct ifreq);
      if ((ifc.ifc_buf=malloc(num_ifrs * sizeof(struct ifreq)))==NULL) {
         err = -ENOMEM;
         goto exit;
      }

      if (ioctl(sock,SIOCGIFCONF,&ifc)<0) {err=errno; goto exit;}

      if (ifc.ifc_len == (num_ifrs * sizeof(struct ifreq))) {
         free(ifc.ifc_buf);
         num_ifrs += 10;
         continue;
      }
      break;
   }

   err = -1; /* default not found */
   ifr = ifc.ifc_req;
   for (i=0; i < ifc.ifc_len; i+= sizeof (struct ifreq), ifr++) {
      if( strcmp(ifr->ifr_name, name) == 0 ) {
         if (ifr->ifr_addr.sa_family == AF_INET6 ) {
            struct sockaddr_in6 *sin = (struct sockaddr_in6 *)&ifr->ifr_addr;
            memcpy(&ip6, &sin->sin6_addr, sizeof(struct in6_addr));
            err = 0;
            break;
         }
         if (ifr->ifr_addr.sa_family == AF_INET ) {
            struct sockaddr_in *sin = (struct sockaddr_in *)&ifr->ifr_addr;
            map_v4_to_v6(&sin->sin_addr, ip6);
            err = 0;
            break;
         }
      }
   }

exit:
   if(ifc.ifc_buf!=NULL) free(ifc.ifc_buf);
   close(sock);
   return err;
}

/**
 * get_ip_for_name - 
 * @name: < 
 * @ip: >
 * 
 * Returns: int
 */
int get_ip_for_name(char *name, struct in6_addr *ip6)
{
   struct hostent *he;
   struct in_addr ip4;

   if( ip6 == NULL ) return -1;

   he = gethostbyname2(name, AF_INET6);
   if( he == NULL ) {
      he = gethostbyname2(name, AF_INET);
      if( he != NULL ) {
         memcpy(&ip4, he->h_addr_list[0], sizeof(struct in_addr));
         map_v4_to_v6(&ip4, ip6);
      }else{
         return -1;
      }
   }else{
      memcpy(&ip6, he->h_addr_list[0], sizeof(struct in6_addr));
   }
   return 0;
}

/**
 * get_name_for_ip - 
 * @name: <> buffer to store name in
 * @nlen: < max len for name
 * @ip: < ip in Be32.
 * 
 * 
 * Returns: int
 */
int get_name_for_ip(char *name, size_t nlen, uint32_t ip)
{
   struct hostent *he;

   he = gethostbyaddr((const char *)&ip, sizeof(uint32_t), AF_INET);
   if( he == NULL ) {
      strncpy(name, iptostr(ip), nlen);
      return 0;
   }
   if( he->h_name == NULL ) {
      strncpy(name, iptostr(ip), nlen);
      return 0;
   }
   strncpy(name, he->h_name, nlen);
   return 0;
}

/**
 * get_ipname - 
 * @str: 
 * 
 * Given a string, which can be a host name, or an ip in v4 or v6
 * return a ip_name_t
 *
 * May need to have a pref to force name lookups towards ipv4
 * 
 * Returns: ip_name_t
 */
ip_name_t *get_ipname(char *str)
{
   ip_name_t *in;
   struct in_addr ip4;
   struct in6_addr ip6;
   struct hostent *he;

   in = malloc(sizeof(ip_name_t));
   if( in == NULL ) return NULL;
   LLi_init( &in->in_list, in);

   /* try ipv6, 
    * if fails, try ipv4
    * if fails, try ipv6 name lookup
    * if fails, try ipv4 name lookup
    * if fails, report error.
    */
   if( inet_pton(AF_INET6, str, &ip6) <=0 ) {
      if( inet_pton(AF_INET, str, &ip4) <=0 ) {
         he = gethostbyname2(str, AF_INET6);
         if( he == NULL ) {
            he = gethostbyname2(str, AF_INET);
            if( he != NULL ) {
               memcpy(&ip4, he->h_addr_list[0], sizeof(struct in_addr));
               map_v4_to_v6(&ip4, &ip6);
            }
         }else{
            memcpy(&ip6, he->h_addr_list[0], sizeof(struct in6_addr));
         }
      }else{
         map_v4_to_v6(&ip4, &ip6);
         he = gethostbyaddr((const char*)&ip4, sizeof(struct in_addr), AF_INET);
      }
   }else{
      he = gethostbyaddr((const char*)&ip6, sizeof(struct in6_addr), AF_INET6);
   }

   if( he == NULL ) in->name = NULL;
   else if( he->h_name == NULL )  in->name = NULL;
   else in->name = strdup(he->h_name);
   memcpy(&in->ip, &ip6, sizeof(struct in6_addr));

   return in;

}

/**
 * print_ipname - 
 * @in: 
 * 
 * 
 * Returns: char
 */
const char *print_ipname(ip_name_t *in)
{
   static char obuf[160];
   char t[80];
   inet_ntop(AF_INET6, &in->ip, t, 80);
   snprintf(obuf, 160, "%s %s", in->name, t);
   return obuf;
}

/* vim: set ai cin et sw=3 ts=3 : */
