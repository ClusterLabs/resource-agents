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
#include <sys/poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/time.h>

#include "gulm_defines.h"
#include "LLi.h"
#include "hash.h"
#include "myio.h"
#include "gio_wiretypes.h"
#include "xdr.h"
#include "core_priv.h"
#include "config_gulm.h"
#include "lock.h"
#include "utils_dir.h"
#include "utils_ip.h"
#include "utils_tostr.h"

/*****************************************************************************/
/* bits of data used by the log_*() and die() functions. */
extern uint32_t verbosity;
extern char *ProgramName;
extern unsigned int shutdown_locked;
extern char *myName;
extern struct in6_addr myIP;
extern int I_am_the;
extern gulm_config_t gulm_config;


typedef struct Resource_s {
   LLi_t r_list;
   char *name;
   int poll_idx;
   uint32_t options;
}Resource_t;

hash_t *Resources;

/* Key accessor functions for hashtable. */
unsigned char *getresname(void *item)
{
   Resource_t *n = (Resource_t*)item;
   return n->name;
}
int getresNln(void *item)
{
   Resource_t *n=(Resource_t*)item;
   return strlen(n->name);
}


/*****************************************************************************/
/**
 * init_resources - 
 */
int init_resources(void)
{
   Resources = hash_create(16, getresname, getresNln);
   if( Resources == NULL ) return -1;
   return 0;
}

/**
 * add_resoruce - 
 * @name: 
 * @poll_idx: 
 * 
 * 
 * Returns: int
 */
int add_resource(char *name, int poll_idx, uint32_t options)
{
   Resource_t *r;

   r = malloc(sizeof(Resource_t));
   if( r == NULL ) return -ENOMEM;
   memset(r,0,sizeof(Resource_t));

   LLi_init(&r->r_list, r);

   r->name = strdup(name);
   if( r->name == NULL ) {
      free(r);
      return -ENOMEM;
   }

   r->poll_idx = poll_idx;
   r->options = options;

   if( r->options & gulm_svc_opt_locked ) shutdown_locked ++;

   return hash_add(Resources, &r->r_list);
}

/**
 * release_resource - 
 * @name: 
 * 
 * 
 * Returns: int
 */
int release_resource(char *name)
{
   LLi_t *tmp;
   Resource_t *r;

   tmp = hash_del(Resources, name, strlen(name));
   if( tmp == NULL ) return -1; /* not found */
   r = LLi_data(tmp);
   if( r->options & gulm_svc_opt_locked ) shutdown_locked --;
   free(r->name);
   free(r);
   return 0;
}

/**
 * die_with_me - 
 * @name: 
 * 
 * returns true if important.
 * 
 * Returns: TRUE or FALSE
 */
int die_with_me(char *name)
{
   LLi_t *tmp;
   Resource_t *r;

   tmp = hash_find(Resources, name, strlen(name));
   if( tmp == NULL ) return FALSE;
   r = LLi_data(tmp);
   return r->options & gulm_svc_opt_important;
}

/**
 * _dump_resource_ - 
 * @item: 
 * @misc: 
 * 
 * 
 * Returns: int
 */
int _dump_resource_(LLi_t *item, void *misc)
{
   FILE *fp = (FILE*)misc;
   Resource_t *r = LLi_data(item);

   fprintf(fp, "Service = %s\n", r->name);
   fprintf(fp, "poller = %d\n", r->poll_idx);
   fprintf(fp, "options = %#x\n\n", r->options);

   return 0;
}

/**
 * dump_resources - 
 */
void dump_resources(void)
{
   FILE *fp;
   int fd;
   if( (fd=open_tmp_file("Gulm_Services")) < 0) return;
   if((fp = fdopen(fd, "a")) == NULL ) return;
   fprintf(fp,"========================================"
         "========================================\n");
   hash_walk(Resources, _dump_resource_, fp);
   fclose(fp);
}

/**
 * _send_one_ - 
 * @item: 
 * @misc: 
 * 
 * 
 * Returns: int
 */
int _send_one_(LLi_t *item, void *misc)
{
   int err;
   xdr_enc_t *enc = (xdr_enc_t *)misc;
   Resource_t *r = LLi_data(item);

   err = xdr_enc_string(enc, r->name);

   return err;
}

/**
 * serialize_resources - 
 * @enc: 
 * 
 */
void serialize_resources(xdr_enc_t *enc)
{
   xdr_enc_uint32(enc, gulm_core_res_list);
   xdr_enc_list_start(enc);
   hash_walk(Resources, _send_one_, enc);
   xdr_enc_list_stop(enc);
   xdr_enc_flush(enc);
}

/*****************************************************************************/
typedef struct {
   char *name;
   uint8_t st;
   struct in6_addr ip;
} smtc_t;
int _send_mbrshp_to_children_(LLi_t *item, void *misc)
{
   smtc_t *s = (smtc_t*)misc;
   Resource_t *r = LLi_data(item);

   if( r->poll_idx > 0 ) {
      /* if not valid, just skip. */
      log_msg(lgm_Subscribers,
            "Sending Membership update \"%s\" about %s to child %s\n",
            gio_mbrupdate_to_str(s->st),
            s->name, r->name);

      if( send_update(r->poll_idx, s->name, s->st, &s->ip) < 0 ) {
         log_err("Error sending sub info to child(%s). (%s)\n",
               r->name, strerror(errno));
         /* really not sure how to handle errors from resources. */
      }
   }

   return 0;
}

/**
 * send_mbrshp_to_children - 
 * @name: 
 * @st: 
 * 
 * passes the membership information up to the children processes.
 *
 * Returns: int
 */
int send_mbrshp_to_children(char *name, int st)
{
   smtc_t s;

   s.name = name;
   s.st = st;

   if( lookup_nodes_ip(name, &s.ip) != 0 ) {
      /* not in node list, use normal lookups. */
      if( get_ip_for_name(name, &s.ip) != 0 ) {
         memset(&s.ip, 0, sizeof(struct in6_addr));
         /* we cannot find it, leave it zero. and let the receiver worry
          * about it.
          */
      }
   }

   return hash_walk(Resources, _send_mbrshp_to_children_, &s);
}

/**
 * _send_core_state_to_children_ - 
 * @item: 
 * @misc: 
 * 
 * 
 * Returns: int
 */
int _send_core_state_to_children_(LLi_t *item, void *misc)
{
   Resource_t *r = LLi_data(item);

   if( r->poll_idx > 0 ) {
      log_msg(lgm_Subscribers,
              "Sending Core state %s to %s\n",
              gio_I_am_to_str(I_am_the), r->name);

      if( send_core_state_update(r->poll_idx) != 0 ) 
         log_err("Error sending core state information to child %s: %s\n",
               r->name, strerror(errno));
   }

   return 0;
}

/**
 * send_core_state_to_children - 
 * Returns: int
 */
int send_core_state_to_children(void)
{
   return hash_walk(Resources, _send_core_state_to_children_, NULL);
}

/* vim: set ai cin et sw=3 ts=3 : */
