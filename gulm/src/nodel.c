/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  2003  All rights reserved.
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
#include <netinet/in.h>

#include "gulm_defines.h"
#include "gio_wiretypes.h"
#include "LLi.h"
#include "hash.h"
#include "config_gulm.h"

/*****************************************************************************/
/* bits of data used by the log_*() and die() functions. */
extern uint32_t verbosity;
extern char *ProgramName;

/*****************************************************************************/

struct nodel_s {
   LLi_t nl_list;
   char *name;
   struct in6_addr ip;
   uint8_t state;
};
typedef struct nodel_s nodel_t;

/* selector functions for the hash tables. */
unsigned char *getnodelname(void *item)
{
   nodel_t *n = (nodel_t*)item;
   return n->name;
}
int getnodelnamelen(void *item)
{
   nodel_t *n=(nodel_t*)item;
   return strlen(n->name);
}

/**
 * initialize_nodel - 
 * 
 * Returns: hash_t
 */
hash_t *initialize_nodel(void)
{
   return hash_create(256, getnodelname, getnodelnamelen);
}


/**
 * update_nodel - 
 * @hsh: 
 * @name: 
 * @ip: 
 * @state: 
 * 
 * updates the state for nodel.
 * adds it if it wasn't all ready there.
 *
 * should i remove on logged out and killed?
 * 
 * Returns: int
 */
int update_nodel(hash_t *hsh, char *name, struct in6_addr *ip, uint8_t state)
{
   LLi_t *tmp;
   nodel_t *n;

   tmp = hash_find(hsh, name, strlen(name));
   if( tmp == NULL ) {
      n = malloc(sizeof(nodel_t));
      if( n == NULL ) return -ENOMEM;
      memset(n, 0, sizeof(nodel_t));
      LLi_init(&n->nl_list, n);
      n->name = strdup(name);
      if( n->name == NULL ) {
         free(n);
         return -ENOMEM;
      }
      memcpy(&n->ip, ip, sizeof(struct in6_addr));
      n->state = state;

      hash_add(hsh, &n->nl_list);
   }else{
      n = LLi_data(tmp);
      n->state = state;
   }
   return 0;
}


/**
 * validate_nodel - 
 * @hsh: 
 * @name: 
 * @ip: 
 * 
 * make sure that name matches ip AND state is logged in.
 * 
 * Returns: TRUE || FALSE
 */
int validate_nodel(hash_t *hsh, char *name, struct in6_addr *ip)
{
   LLi_t *tmp;
   nodel_t *n;

   tmp = hash_find(hsh, name, strlen(name));
   if( tmp == NULL ) return FALSE;
   n = LLi_data(tmp);

   if( !IN6_ARE_ADDR_EQUAL(ip->s6_addr32, n->ip.s6_addr32) ) return FALSE;

   if( n->state != gio_Mbr_Logged_in ) return FALSE;

   return TRUE;
}

/* vim: set ai cin et sw=3 ts=3 : */
