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
#include "core_priv.h"
#include "config_gulm.h"
#include "utils_dir.h"
#include "utils_ip.h"
#include "utils_tostr.h"


/*****************************************************************************/
/* bits of data used by the log_*() and die() functions. */
extern uint32_t verbosity;
extern char *ProgramName;

extern gulm_config_t gulm_config;

hash_t *Nodes_by_Name;
LLi_t heartbeat_lru; /* next is MRU, prev is LRU */
/*****************************************************************************/
typedef struct node_s {
   LLi_t By_Name;
   LLi_t lru;
   
   char *Name;
   struct in6_addr ip;

   uint8_t State;
   uint8_t last_state;
   uint8_t mode; /* Slave, Pending, Arbitrating, Master, Client */
   uint8_t sweepcheck;

   unsigned int missed_beats;
   uint64_t last_beat;
   uint64_t delay_avg; /*average amount of time between beats */
   uint64_t max_delay;

   /* stuff for sending data to nodes. */
   int poll_idx;

} Node_t;

/* selector functions for the hash tables. */
unsigned char *getnodename(void *item)
{
   Node_t *n = (Node_t*)item;
   return n->Name;
}
int getnodeNln(void *item)
{
   Node_t *n=(Node_t*)item;
   return strlen(n->Name);
}

/**
 * move_to_mru - 
 * @n: 
 * 
 * 
 * Returns: void
 */
void move_to_mru(Node_t *n)
{
   LLi_del(&n->lru);
   LLi_unhook(&n->lru);
   LLi_add_after(&heartbeat_lru, &n->lru);
}

/**
 * remove_from_lru - 
 * @n: 
 * 
 * 
 * Returns: void
 */
void remove_from_lru(Node_t *n)
{
   if( ! LLi_empty(&n->lru) ) { /* not really empty, but same idea. */
      LLi_del(&n->lru);
      LLi_unhook(&n->lru);
   }
}

/**
 * init_nodes - 
 * 
 * Returns: int
 */
int init_nodes(void)
{
   LLi_init_head(&heartbeat_lru);
   Nodes_by_Name = hash_create(256, getnodename, getnodeNln);
   if( Nodes_by_Name == NULL ) return -1;
   return 0;
}

/**
 * _release_nodelist_ - 
 * @item: 
 * @misc: 
 * 
 * innear working function.
 * 
 * Returns: int
 */
int _release_nodelist_(LLi_t *item, void *misc)
{
   Node_t *n = LLi_data(item);
   LLi_del(item);
   remove_from_lru(n);
   free(n->Name);
   free(n);
   return 0;
}
/**
 * release_nodelist - 
 * 
 * free everything.
 */
void release_nodelist(void)
{
   hash_walk(Nodes_by_Name, _release_nodelist_, NULL);
   LLi_init_head(&heartbeat_lru);
}

/*****************************************************************************/
struct _send_mbrshp_to_node_s {
   char *name;
   uint8_t st;
   struct in6_addr ip;
};

/**
 * _send_mbrshp_to_node - 
 * @item: 
 * @misc: 
 * 
 * 
 * Returns: int
 */
int _send_mbrshp_to_node(LLi_t *item, void *misc)
{
   Node_t *n;
   struct _send_mbrshp_to_node_s *t = (struct _send_mbrshp_to_node_s *)misc;
   int err;
   n = LLi_data(item);

   if( n->mode == gio_Mbr_ama_Master ||
       n->mode == gio_Mbr_ama_Arbitrating )
      return 0; /* skip ourself */

   if( n->State == gio_Mbr_Logged_in ) {

      log_msg(lgm_Subscribers, "Sending Membership update \"%s\" about %s "
            "to slave %s\n", gio_mbrupdate_to_str(t->st), t->name, n->Name);
      err=send_update(n->poll_idx, t->name, t->st, &t->ip);
      if( err != 0 ) {
         log_msg(lgm_Always,"Could not send membership update \"%s\" about %s "
            "to slave %s\n", gio_mbrupdate_to_str(t->st), t->name, n->Name);
      }
   }else
   if( n->State == gio_Mbr_OM_lgin ) {
      log_msg(lgm_Always, "Member update message %s about %s to %s is lost "
            "because node is in OM\n",
            gio_mbrupdate_to_str(t->st), t->name, n->Name);
      /* Note, as tempting as it maybe to put a queue here to hold onto
       * these until the node reconnects, it typically doesn't do what you
       * would expect.  Why? Well because of the clients do not log into
       * Arbitrating nodes.  What happens is that the node will try to
       * login, see that we are Arbitrating, and then logout, lossing the
       * queue.  Yes the queued messages can get sent, but the client will
       * have never left the Pending state, so in most cases it won't do
       * anything.
       * Of course sometimes things race differently and it will actually
       * do what you expect.
       *
       * Races are like that.
       */
   }

   return 0;
}

/**
 * send_mbrshp_to_slaves - 
 * @name: 
 * @st: 
 * 
 * send a membership update out to the slave cores.
 * Only the Master should ever call this.
 * 
 * Returns: int
 */
int send_mbrshp_to_slaves(char *name, int st)
{
   struct _send_mbrshp_to_node_s t;

   t.name = name;
   t.st = st;

   if( lookup_nodes_ip(name, &t.ip) != 0 ) {
      /* not in node list, use normal lookups. */
      if( get_ip_for_name(name, &t.ip) != 0 ) {
         memset(&t.ip, 0, sizeof(struct in6_addr));
         /* we cannot find it, leave it zero. and let the receiver worry
          * about it.
          */
      }
   }

   return hash_walk(Nodes_by_Name, _send_mbrshp_to_node, &t);
}

/*****************************************************************************/
/**
 * print_node - 
 * @FP: 
 * @n: 
 * 
 * Returns: void
 */
void print_node(FILE *FP, Node_t *n)
{
   struct timeval tv;
   uint64_t fulltime;
   gettimeofday(&tv, NULL);
   fulltime = tvs2uint64(tv);
   fprintf(FP, "     name: %s\n", n->Name);
   switch(n->State) {
#define CP(x) case (x): fprintf(FP,"    State: %s\n",#x); break
      CP(gio_Mbr_Logged_in);
      CP(gio_Mbr_Logged_out);
      CP(gio_Mbr_Expired);
      CP(gio_Mbr_OM_lgin);
      default: fprintf(FP,"Unknown node state!!!! %d\n",n->State);break;
#undef CP
   }
   switch(n->last_state) {
#define CP(x) case (x): fprintf(FP,"lastState: %s\n",#x); break
      CP(gio_Mbr_Logged_in);
      CP(gio_Mbr_Logged_out);
      CP(gio_Mbr_Expired);
      CP(gio_Mbr_OM_lgin);
      default: fprintf(FP,"Unknown node state!!!! %d\n",n->State);break;
#undef CP
   }
   fprintf(FP, "   missed: %u\n",n->missed_beats);
   fprintf(FP, "     last: %"PRIu64"\n", n->last_beat);
   fprintf(FP, "  current: %"PRIu64"\n", fulltime);
   fprintf(FP, "      avg: %"PRIu64"\n", n->delay_avg);
   fprintf(FP, "      max: %"PRIu64"\n", n->max_delay);

}

int _print_one_node_(LLi_t *item, void *misc)
{
   Node_t *n;
   FILE *FP;
   n = LLi_data(item);
   FP = (FILE*)misc;
   print_node(FP,n);
   fprintf(FP,"\n");
   return 0;
}
int fdump_nodes(FILE *f)
{return hash_walk(Nodes_by_Name, _print_one_node_, f);}

/**
 * list_heartbeat_lru - 
 * @fp: 
 */
void list_heartbeat_lru(FILE *fp)
{
   LLi_t *tmp;
   Node_t *n;
   fprintf(fp,"======================\nheartbeat_lru\n");
   for(tmp = LLi_prev(&heartbeat_lru) ;
       NULL != LLi_data(tmp) ;
       tmp = LLi_prev(tmp)) {
      n = LLi_data(tmp);
      fprintf(fp," %s\n", n->Name);
   }
}

/**
 * dump_nodes - 
 * 
 */
void dump_nodes(void)
{
   FILE *fp;
   char *path;
   if( build_tmp_path("Gulm_Nodelist", &path) != 0) return;
   if((fp = fopen(path, "a")) == NULL ) {free(path); return;}
   fprintf(fp,"========================================"
         "========================================\n");
   fdump_nodes(fp);
   list_heartbeat_lru(fp);
   fclose(fp);
   free(path);
}

/**
 * encode_one_node - 
 * @enc: 
 * @N: 
 * 
 * 
 * Returns: int
 */
int encode_one_node(xdr_enc_t *enc, Node_t *N)
{
   int err;
   if((err=xdr_enc_string(enc, N->Name)) <0) return err;
   if((err=xdr_enc_ipv6(enc, &N->ip)) <0) return err;
   if((err=xdr_enc_uint8(enc, N->State)) <0) return err;
   if((err=xdr_enc_uint8(enc, N->last_state)) <0) return err;
   if((err=xdr_enc_uint8(enc, N->mode)) <0) return err;
   if((err=xdr_enc_uint32(enc, N->missed_beats)) <0) return err;
   if((err=xdr_enc_uint64(enc, N->last_beat)) <0) return err;
   if((err=xdr_enc_uint64(enc, N->delay_avg)) <0) return err;
   if((err=xdr_enc_uint64(enc, N->max_delay)) <0) return err;

   return 0;
}

/**
 * add_node - 
 * @name: 
 * @type: 
 * @ip: 
 * @stomith: 
 * 
 * 
 * Returns: int
 */
int add_node(char *name, struct in6_addr *ip)
{
   LLi_t *tmp;
   Node_t *n;
   int e=-3;

   tmp = hash_find(Nodes_by_Name, name, strlen(name));
   if( tmp == NULL ) {
      n = malloc(sizeof(Node_t));
      if( n == NULL ) return -1;
      memset(n,0,sizeof(Node_t));
      LLi_init(&n->By_Name, n);
      LLi_init(&n->lru, n);
      n->State = gio_Mbr_Logged_out;
      n->last_state = gio_Mbr_Logged_out;
      n->poll_idx = -1;
      n->Name = strdup(name);
      memcpy(&n->ip, ip, sizeof(struct in6_addr));
      if(n->Name == NULL) goto fail;

      e = hash_add(Nodes_by_Name, &n->By_Name);
      if( e<0) {
         log_err("Failed to add %s(%s) to Name hash. %s\n", name, ip6tostr(ip),
               (e==-1)?"All ready in table":"");
         goto fail;
      }
   }else{
      n = LLi_data(tmp);
   }

   return 0;
fail:
   if(n->Name != NULL) free(n->Name);

   free(n);
   return e;
}

/**
 * get_node - 
 * @name: 
 * 
 * 
 * Returns: int
 */
int get_node(xdr_enc_t *enc, char *name)
{
   LLi_t *tmp = NULL;
   int err = gio_Err_Ok;

   if( name != NULL )
      tmp = hash_find(Nodes_by_Name, name, strlen(name));

   do {
      if((err=xdr_enc_uint32(enc, gulm_core_mbr_lstrpl)) <0) break;
      if((err=xdr_enc_list_start(enc)) <0) break;

      if( tmp != NULL )
         if((err=encode_one_node(enc, LLi_data(tmp))) <0) break;

      if((err=xdr_enc_list_stop(enc)) <0) break;
      if((err=xdr_enc_flush(enc)) <0) break;
   }while(0);

   return err;
}

/**
 * _get_all_nodes_ - 
 * @item: 
 * @misc: 
 * 
 * 
 * Returns: int
 */
int _get_all_nodes_(LLi_t *item, void *misc)
{
   xdr_enc_t *enc = (xdr_enc_t*)misc;
   Node_t *n = LLi_data(item);
   return encode_one_node(enc, n);
}
/**
 * serialize_node_list - 
 * @enc: 
 * 
 * 
 * Returns: int
 */
int serialize_node_list(xdr_enc_t *enc)
{
   int err = 0;

   do {
      if((err=xdr_enc_uint32(enc, gulm_core_mbr_lstrpl)) <0) break;
      if((err=xdr_enc_list_start(enc)) <0) break;

      if((err=hash_walk(Nodes_by_Name, _get_all_nodes_, enc)) <0) break;

      if((err=xdr_enc_list_stop(enc)) <0) break;
      if((err=xdr_enc_flush(enc)) <0) break;
   }while(0);

   return err;
}

/**
 * deserialize_node_list - 
 * @dec: 
 * 
 * 
 * Returns: int
 */
int deserialize_node_list(xdr_dec_t *dec)
{
   int err;
   uint8_t *x_name;
   uint32_t x_code;
   LLi_t *tmp;
   Node_t *n;

   if((err=xdr_dec_uint32(dec, &x_code)) < 0) return err;
   if((err=xdr_dec_list_start(dec)) < 0) return err;
   while( xdr_dec_list_stop(dec) != 0 ) {

      /* get name. check to see if it exists. */
      if((err=xdr_dec_string(dec, &x_name)) < 0) return err;
      if( x_name == NULL ) {return -ENOMEM;}

      tmp = hash_find(Nodes_by_Name, x_name, strlen(x_name));
      if( tmp == NULL ) {
         n = malloc(sizeof(Node_t));
         if( n == NULL ) return -ENOMEM;
         memset(n, 0, sizeof(Node_t));
         n->Name = x_name;
         LLi_init( &n->By_Name, n);
         LLi_init(&n->lru, n);
         n->poll_idx = -1;
         err = hash_add(Nodes_by_Name, &n->By_Name);
         if( err != 0 ) {
            /* should not get here, since we just checked to see that this
             * name was not in the hash.
             */
            log_err("Failed to add %s to the Name hash.\n", x_name);
            free(x_name);
            free(n);
            return err;
         }
      } else {
         n = LLi_data(tmp);
         free(x_name); /* don't need this, as it is already in the struct */
         x_name = NULL;
         /* we're not master if we're here, so we know there can be no
          * connections.  Mark as such.
          */
         n->poll_idx = -1;
      }
      n->sweepcheck = 0;

      if((err=xdr_dec_ipv6(dec, &(n->ip) )) < 0) return err;
      if((err=xdr_dec_uint8(dec, &(n->State) )) < 0) return err;
      if((err=xdr_dec_uint8(dec, &(n->last_state) )) < 0) return err;
      if((err=xdr_dec_uint8(dec, &(n->mode) )) < 0) return err;
      if((err=xdr_dec_uint32(dec, &(n->missed_beats))) < 0) return err;
      if((err=xdr_dec_uint64(dec, &(n->last_beat))) < 0) return err;
      if((err=xdr_dec_uint64(dec, &(n->delay_avg))) < 0) return err;
      if((err=xdr_dec_uint64(dec, &(n->max_delay))) < 0) return err;

   }
   return 0;
}

/**
 * lookup_nodes_ip - 
 * @name: 
 * @ip: 
 * 
 * Gets the ip of a logged in node.
 * 
 * Returns: int
 */
int lookup_nodes_ip(char *name, struct in6_addr *ip)
{
   LLi_t *tmp;
   Node_t *n;

   tmp = hash_find(Nodes_by_Name, name, strlen(name));
   if( tmp == NULL) return gio_Err_Unknown_Cs;

   n = LLi_data(tmp);
   memcpy(ip, &n->ip, sizeof(struct in6_addr));
   return 0;
}

/**
 * set_nodes_mode - 
 * @name: 
 * @mode: 
 * 
 * 
 * Returns: int
 */
int set_nodes_mode(char *name, int mode)
{
   LLi_t *tmp;
   Node_t *n;

   tmp = hash_find(Nodes_by_Name, name, strlen(name));
   if( tmp == NULL) return gio_Err_Unknown_Cs;

   n = LLi_data(tmp);
   n->mode = mode;

   return gio_Err_Ok;
}

/**
 * Mark_Loggedin - 
 * @name: 
 * 
 * 
 * Returns: =0:ok, !0:Error
 */
int Mark_Loggedin(char *name)
{
   LLi_t *tmp;
   Node_t *n;

   tmp = hash_find(Nodes_by_Name, name, strlen(name));
   if( tmp == NULL) return gio_Err_Unknown_Cs;

   n = LLi_data(tmp);
   if( n->State != gio_Mbr_Logged_out &&
       n->State != gio_Mbr_OM_lgin )
      return gio_Err_BadStateChg;

   n->last_state = n->State;
   n->State = gio_Mbr_Logged_in;

   /* put onto MRU */
   move_to_mru(n);

   return gio_Err_Ok;
}

/**
 * Mark_Loggedout - 
 * @name: 
 * 
 * loggedout == deleted.
 * 
 * Returns: =0:ok, !0:Error
 */
int Mark_Loggedout(char *name)
{
   LLi_t *tmp;
   Node_t *n;

   tmp = hash_find(Nodes_by_Name, name, strlen(name));
   if( tmp == NULL) return gio_Err_Unknown_Cs;

   n = LLi_data(tmp);
   remove_from_lru(n);
   n->last_state = n->State;
   n->State = gio_Mbr_Logged_out;
   n->poll_idx = -1;

   return gio_Err_Ok;
}

/**
 * Mark_lgout_from_Exp - 
 * @name: 
 * 
 * full logout, so free struct.
 * 
 * Returns: =0:ok, !0:Error
 */
int Mark_lgout_from_Exp(char *name)
{
   LLi_t *tmp;
   Node_t *n;

   tmp = hash_find(Nodes_by_Name, name, strlen(name));
   if( tmp == NULL) return gio_Err_Unknown_Cs;

   n = LLi_data(tmp);
   remove_from_lru(n);
   n->last_state = n->State;
   n->State = gio_Mbr_Logged_out;
   n->poll_idx = -1;
   return gio_Err_Ok;
}

/**
 * Mark_Expired - 
 * @name: 
 * 
 * for when slave gets updates from master.
 * 
 * Returns: =0:ok, !0:Error
 */
int Mark_Expired(char *name)
{
   LLi_t *tmp;
   Node_t *n;

   tmp = hash_find(Nodes_by_Name, name, strlen(name));
   if( tmp == NULL) return gio_Err_Unknown_Cs;

   n = LLi_data(tmp);
   remove_from_lru(n);

   n->last_state = n->State;
   n->State = gio_Mbr_Expired;
   n->poll_idx = -1;

   return gio_Err_Ok;
}

/**
 * Die_if_expired - 
 * @name: 
 * 
 */
void Die_if_expired(char *name)
{
   LLi_t *tmp;
   Node_t *n;

   tmp = hash_find(Nodes_by_Name, name, strlen(name));
   if( tmp == NULL) return;

   n = LLi_data(tmp);

   if( n->State == gio_Mbr_Expired ) {
      die(ExitGulm_BadLogic,"I cannot run when marked expired.\n");
   }

}

/**
 * do_node_Expired - 
 * @n: 
 * 
 * 
 * Returns: void
 */
void do_node_Expired(Node_t *n)
{
   log_msg(lgm_Stomith,"Client (%s) expired\n", n->Name);
   n->State = gio_Mbr_Expired;

   remove_from_lru(n);

   /* send first so that the expired msg can make it to the expired node if
    * it happens to be in one of those messed up cases where it is still
    * connected but actually expired.
    */
   send_mbrshp_to_children(n->Name, gio_Mbr_Expired);
   send_mbrshp_to_slaves(n->Name, gio_Mbr_Expired);

   close_by_idx(n->poll_idx);

   n->poll_idx = -1;

   queue_node_for_fencing(n->Name);
}

/**
 * Force_Node_Expire - 
 * @name: 
 * 
 * 
 * Returns: int
 */
int Force_Node_Expire(char *name)
{
   LLi_t *tmp;
   Node_t *n;

   tmp = hash_find(Nodes_by_Name, name, strlen(name));
   if( tmp == NULL) return gio_Err_Unknown_Cs;

   n = LLi_data(tmp);

   do_node_Expired(n);
   return gio_Err_Ok;
}

/**
 * beat_node - 
 * @name: < 
 * @poll_idx: < which entry in the pollers
 * 
 * Returns: =0:Ok, !0:Error
 */
int beat_node(char *name, int poll_idx)
{
   LLi_t *tmp;
   Node_t *n;
   struct timeval tv;
   uint64_t fulltime;

   gettimeofday(&tv, NULL);
   fulltime = tvs2uint64(tv);

   tmp = hash_find(Nodes_by_Name, name, strlen(name));
   if( tmp == NULL) return gio_Err_Unknown_Cs;

   n = LLi_data(tmp);

   if( n->State != gio_Mbr_Logged_in && n->State != gio_Mbr_OM_lgin ) {
      log_err("Cannot heartbeat if not logged in.\n");
      print_node(stderr,n);
      return gio_Err_NotAllowed;
   }

   log_msg(lgm_Heartbeat, "Got heartbeat from %s at %"PRIu64" "
         "(last:%"PRIu64" max:%"PRIu64" avg:%"PRIu64")\n",
         n->Name, fulltime,
         (fulltime - n->last_beat),
         n->max_delay, n->delay_avg);

   if( n->last_beat != 0 ) {
      if( n->delay_avg != 0) 
         n->delay_avg = ( n->delay_avg + (fulltime - n->last_beat) ) /2;
      else
         n->delay_avg = fulltime - n->last_beat;
      n->max_delay = MAX(n->max_delay, (fulltime - n->last_beat) );
   }

   n->last_beat = fulltime;
   n->missed_beats = 0;
   n->poll_idx = poll_idx;
   move_to_mru(n);

   return gio_Err_Ok;
}

/**
 * check_beats - 
 * 
 * Returns: int
 */
int check_beats(void)
{
   static uint64_t lastrun=0;
   uint64_t fulltime;
   struct timeval tv;
   LLi_t *tmp, *nxt;
   Node_t *n;

   gettimeofday(&tv,NULL);
   fulltime = tvs2uint64(tv);

   if( fulltime > lastrun + (gulm_config.heartbeat_rate/2) ) {
      lastrun = fulltime;

      /* walk the lru until last_beat + timeout is more than current. */
      for(tmp = LLi_prev(&heartbeat_lru) ;
          NULL != LLi_data(tmp) ;
          tmp = nxt) {
         nxt = LLi_prev(tmp);

         n = LLi_data(tmp);

         if( n->State == gio_Mbr_Logged_in ||
             n->State == gio_Mbr_OM_lgin ) {

            if( fulltime > n->last_beat + gulm_config.heartbeat_rate ) {
               n->missed_beats++;
               /* put current into last_beat so that we will wait the full
                * timeout rate before incrementing the missed_beats again.
                */
               n->last_beat = fulltime;

               /* since this is a virtual heartbeat of sorts, put onto MRU */
               move_to_mru(n);

               log_msg(lgm_Network, "%s missed a heartbeat (time:%"PRIu64
                     " mb:%d)\n",
                     n->Name, fulltime, n->missed_beats);
            }else{
               /* this node has heartbeated in time.
                * Therefor, everyone else in this list has also sent
                * heartbeats in time.
                * So we're done scanning the list.
                */
               break;
            }

            if( n->missed_beats > gulm_config.allowed_misses ) {
               do_node_Expired(n);
            }
         }/* logged in? */
      }/* for items in list */
   }

   return gio_Err_Ok;
}


/**
 * _inner_beat_all_ - 
 * @item: 
 * @misc: 
 * 
 * 
 * Returns: int
 */
int _inner_beat_all_(LLi_t *item, void *misc)
{
   Node_t *n;
   n = LLi_data(item);

   if( n->State == gio_Mbr_Logged_in ||
       n->State == gio_Mbr_OM_lgin) {
      beat_node(n->Name, -1);
   }
   return 0;
}

/**
 * beat_all_once - 
 * 
 * This is for when Node was slave, and becomes Master.  All of the Node
 * entries have zeros in the time fields.  So when we call check_beats()
 * every node will get fenced.  We need to give them time to get logged
 * back in and start heartbeating again.  And this is how we do it.
 * 
 * Returns: int
 */
int beat_all_once(void)
{
   return hash_walk(Nodes_by_Name, _inner_beat_all_, NULL);
}

int _inner_fence_expired (LLi_t *item, void *misc)
{
   Node_t *n;
   n = LLi_data(item);

   if( n->State == gio_Mbr_Expired ) {
      queue_node_for_fencing(n->Name);
   }
   return 0;
}
/**
 * fence_all_expired -
 *
 * fences all expired nodes in the list
 *
 * Returns: int
 */
int fence_all_expired(void)
{
   return hash_walk(Nodes_by_Name, _inner_fence_expired, NULL);
}


/**
 * _inner_Mark_Old_Master_lgin - 
 * @item: 
 * @misc: 
 * 
 * 
 * Returns: int
 */
int _inner_Mark_Old_Master_lgin(LLi_t *item, void *misc)
{
   Node_t *n;
   n = LLi_data(item);

   if( n->State == gio_Mbr_Logged_in) {
      n->last_state = n->State;
      n->State = gio_Mbr_OM_lgin;
   }
   return 0;
}
int Mark_Old_Master_lgin(void)
{
   return hash_walk(Nodes_by_Name, _inner_Mark_Old_Master_lgin, NULL);
}

/**
 * _inner_Logout_leftovers - 
 * @item: 
 * @misc: 
 * 
 * 
 * Returns: int
 */
int _inner_Logout_leftovers(LLi_t *item, void *misc)
{
   Node_t *n;
   n = LLi_data(item);

   if( n->sweepcheck == 42 ) {
      if( n->State == gio_Mbr_Logged_out ) {
         if( n->last_state == gio_Mbr_Logged_in ) {
            log_msg(lgm_Always, "Node %s logged out while we were without a "
                  "Master.\n", n->Name);
            send_mbrshp_to_children(n->Name, gio_Mbr_Logged_out);
         }else
         if( n->last_state == gio_Mbr_Logged_in ) {
            log_msg(lgm_Always, "Node %s Expired and Fenced while we were "
                  "without a Master.\n", n->Name);
            send_mbrshp_to_children(n->Name, gio_Mbr_Expired);
            send_mbrshp_to_children(n->Name, gio_Mbr_Killed);
         }
      }else
      if( n->State == gio_Mbr_Expired ) {
         /* they haven't been killed yet. but soon.... */
            log_msg(lgm_Always, "Node %s Expired while we were "
                  "without a Master.\n", n->Name);
            send_mbrshp_to_children(n->Name, gio_Mbr_Expired);
      }

      remove_from_lru(n);
      n->poll_idx = -1;
   }
   return 0;
}
int Logout_leftovers(void)
{
   return hash_walk(Nodes_by_Name, _inner_Logout_leftovers, NULL);
}
int _inner_tag_for_lost(LLi_t *item, void *misc)
{
   Node_t *n;
   n = LLi_data(item);

   if( n->State != gio_Mbr_Logged_out)
      n->sweepcheck = 42;

   return 0;
}
int tag_for_lost(void)
{
   return hash_walk(Nodes_by_Name, _inner_tag_for_lost, NULL);
}

/* vim: set ai cin et sw=3 ts=3 : */
