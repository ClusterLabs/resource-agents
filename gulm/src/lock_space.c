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
#include <sys/time.h>
#include <signal.h>

#include "gulm_defines.h"
#include "hash.h"
#include "LLi.h"
#include "Qu.h"
#include "gio_wiretypes.h"
#include "xdr.h"
#include "lock_priv.h"
#include "utils_tostr.h"
#include "utils_dir.h"
#include "config_gulm.h"

/*****************************************************************************/
/* bits of data used by the log_*() and die() functions. */
extern uint32_t verbosity;
extern char *ProgramName;

extern char *LTname;
extern gulm_config_t gulm_config;

/*****************************************************************************/
#ifdef DEBUG_LVB
#define lvb_log_msg(fmt, args...) log_msg(lgm_Always , fmt , ## args )
#else /*DEBUG_LVB*/
#define lvb_log_msg(fmt, args...)
#endif /*DEBUG_LVB*/
/*****************************************************************************/
/* Not a true maximum.  This is actually just the point where we start
 * sending dropall requests to the clients.
 */
unsigned long max_locks     = 1024 * 1024;

/* these count the number of holders in each state or type. */
unsigned long cnt_exl_holds = 0;
unsigned long cnt_shd_holds = 0;
unsigned long cnt_dfr_holds = 0;
unsigned long cnt_lvb_holds = 0;
unsigned long cnt_exp_holds = 0;

unsigned long cur_lops = 0;
unsigned long cnt_conflicts = 0;

unsigned long cnt_inq = 0;
unsigned long cnt_confq = 0;
unsigned long cnt_replyq = 0;

unsigned long cnt_locks     = 0; /* aka used_locks */
unsigned long free_locks = 0;
unsigned long free_lkrqs = 0;
unsigned long used_lkrqs = 0;
unsigned long free_holders = 0;
unsigned long used_holders = 0;
/*****************************************************************************/
/* global lock store */
hash_t *AllLocks=NULL;
/* accessors. */
unsigned char *getlkname(void *item) {
   Lock_t *c = (Lock_t*)item;
   return c->key;
}
int getlknlen(void *item) {
   Lock_t *c = (Lock_t*)item;
   return c->keylen;
}

/* the empties list.
 * We keep empties around instead of freeing them.
 * Saves mallocs.
 */
LLi_t Free_lkrq;
LLi_t Free_lock;
LLi_t Free_Holders;

/**
 * prealloc_holders - 
 * Returns: =0:Ok, !0:Error
 */
int prealloc_holders(void)
{
   int i;
   Holders_t *h;
   for(i=0; i < gulm_config.lt_preholds; i++ ) {
      h = malloc(sizeof(Holders_t));
      if( h == NULL ) return -ENOMEM;
      LLi_init( &h->cl_list, h );
      LLi_add_after( &Free_Holders, &h->cl_list );
      free_holders ++;
   }
   return 0;
}

/**
 * prealloc_locks - 
 * Returns: =0:Ok, !0:Error
 */
int prealloc_locks(void)
{
   int i;
   Lock_t *lk;
   for(i=0; i < gulm_config.lt_prelocks; i ++ ) {
      lk = malloc(sizeof(Lock_t));
      if( lk == NULL ) return -ENOMEM;
      LLi_init( &lk->lk_list, lk);
      LLi_add_after( &Free_lock, &lk->lk_list );
      free_locks ++;
   }
   return 0;
}

/**
 * prealloc_lkrq - 
 * Returns: =0:Ok, !0:Error
 */
int prealloc_lkrqs(void)
{
   int i;
   Waiters_t *lkrq;
   for(i=0; i < gulm_config.lt_prelkrqs; i++ ) {
      lkrq = malloc(sizeof(Waiters_t));
      if( lkrq == NULL ) return -ENOMEM;
      LLi_init( &lkrq->wt_list, lkrq );
      LLi_add_after( &Free_lkrq, &lkrq->wt_list );
      free_lkrqs ++;
   }
   return 0;
}

/**
 * init_lockspace - 
 * 
 * Should the size of the locktable hash be configuriable?
 *
 * Returns: int
 */
int init_lockspace(unsigned long maxlocks, unsigned int hashbuckets)
{
   LLi_init_head(&Free_lkrq);
   LLi_init_head(&Free_lock);
   LLi_init_head(&Free_Holders);
   max_locks = maxlocks;
   AllLocks = hash_create(hashbuckets, getlkname, getlknlen);
   if( AllLocks == NULL ) return -ENOMEM;
   if( prealloc_locks() != 0 ) return -ENOMEM;
   if( prealloc_lkrqs() != 0 ) return -ENOMEM;
   if( prealloc_holders() != 0 ) return -ENOMEM;
   return 0;
}

/*****************************************************************************/
/**
 * buftob64 - 
 * @ibuf: 
 * @ilen: 
 * @obuf: 
 * @olen: 
 * 
 * 
 * Returns: char
 */
char *buftob64(uint8_t *ibuf, uint8_t ilen, uint8_t *obuf, uint8_t olen)
{
   static char *b64string = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
   int c;
   int i, b=0;

   for(i=0; i < ilen; i++ ) {
      c = (ibuf[i] >> 2) & 0x3f;
      obuf[b++] = b64string[c];
      c = (ibuf[i] << 4) & 0x3f;
      if( ++i < ilen) {
         c |= (ibuf[i] >> 4) & 0x0f;
      }
      obuf[b++] = b64string[c];

      if( i < ilen ) {
         c = (ibuf[i] << 2) & 0x3f;
         if( ++i < ilen) {
            c |= (ibuf[i] >> 6) & 0x03;
         }
         obuf[b++] = b64string[c];
      }else{
         ++i;
         obuf[b++] = '=';
      }

      if( i < ilen ) {
         c = ibuf[i] & 0x3f;
         obuf[b++] = b64string[c];
      }else{
         obuf[b++] = '=';
      }
   }

   obuf[b] = '\0';

   return obuf;
}

char *lkeytob64(uint8_t *key, uint8_t keylen)
{
   static char buf[256];
   return buftob64(key, keylen, buf, (uint8_t)256);
}

char *lvbtob64(uint8_t *key, uint8_t keylen)
{
   static char buf[256];
   return buftob64(key, keylen, buf, (uint8_t)256);
}

/**
 * print_wait_queue - 
 * @FP: 
 * @w: 
 */
void print_waiter(FILE *FP, Waiters_t *w)
{
         fprintf(FP," - key        : '%s'\n", lkeytob64(w->key, w->keylen));
         fprintf(FP,"   name       : %s\n", w->name);
         fprintf(FP,"   subid      : %"PRIu64"\n", w->subid);
         switch(w->state) {
#define CasePrint(x) case (x): fprintf(FP,"   state      : %s\n",#x); break
            CasePrint(gio_lck_st_Unlock);
            CasePrint(gio_lck_st_Exclusive);
            CasePrint(gio_lck_st_Deferred);
            CasePrint(gio_lck_st_Shared);
#undef CasePrint
            default: fprintf(FP,"   state      : %d(unknown)\n",w->state);break;
         }
         fprintf(FP,"   flags      : %#x\n", w->flags);
         fprintf(FP,"   start      : %"PRIu64"\n", w->start);
         fprintf(FP,"   stop       : %"PRIu64"\n", w->stop);
         if( w->LVB == NULL ) {
            fprintf(FP,"   LVB        :\n");
         }else{
            fprintf(FP,"   LVB        : '%s'\n", lvbtob64(w->LVB, w->LVBlen));
         }
         fprintf(FP,"   Slave_rply : 0x%x\n", w->Slave_rpls);
         fprintf(FP,"   Slave_sent : 0x%x\n", w->Slave_sent);
         fprintf(FP,"   idx        : %d\n", w->idx);
#ifdef LOCKHISTORY
         fprintf(FP,"   ret        : %d\n", w->ret);
         fprintf(FP,"   starttime  : %"PRIu64"\n", w->starttime);
         fprintf(FP,"   stoptime   : %"PRIu64"\n", w->stoptime);
#endif
}

/**
 * print_wait_queue - 
 * @FP: 
 * @list: 
 */
void print_wait_queue(FILE *FP, LLi_t *list)
{
   LLi_t *tp;
   Waiters_t *w;
   if( ! LLi_empty(list) ) {
      for(tp=LLi_next(list);LLi_data(tp) != NULL;tp=LLi_next(tp)){
         w = LLi_data(tp);
         print_waiter(FP, w);
      }
   }
}

/**
 * print_holder - 
 * @FP: 
 * @h: 
 */
void print_holder(FILE *FP, Holders_t *h)
{
   fprintf(FP," - name   : %s\n", h->name);
   fprintf(FP,"   subid  : %"PRIu64"\n", h->subid);
   fprintf(FP,"   state  : ");
   switch(h->state) {
#define CasePrint(x) case (x): fprintf(FP,"%s\n",#x); break
      CasePrint(gio_lck_st_Unlock);
      CasePrint(gio_lck_st_Exclusive);
      CasePrint(gio_lck_st_Deferred);
      CasePrint(gio_lck_st_Shared);
#undef CasePrint
      default: fprintf(FP,"%d(unknown)\n", h->state); break;
   }
   fprintf(FP,"   start  : %"PRIu64"\n", h->start);
   fprintf(FP,"   stop   : %"PRIu64"\n", h->stop);
   fprintf(FP,"   flags  : %#x\n", h->flags);
   fprintf(FP,"   idx    : %d\n", h->idx);
}

/**
 * print_holder_list - 
 * @FP: 
 * @list: 
 */
void print_holder_list(FILE *FP, LLi_t *list)
{
   LLi_t *tp;
   Holders_t *h;

   if( ! LLi_empty( list ) ) {
      for(tp=LLi_next(list); LLi_data(tp) != NULL; tp=LLi_next(tp)) {
         h = LLi_data(tp);
         print_holder(FP, h);
      }
   }
}

/**
 * print_lock - print out a lock
 * @lk: < lock struct
 * 
 * for debuggen
 *
 * output is valid yaml  (for parsen)
 *
 */
void print_lock(FILE *FP, Lock_t *lk)
{
   fprintf(FP,"---\nkey            : '%s'\n", lkeytob64(lk->key, lk->keylen));
   fprintf(FP,"LVBlen         : %d\n", lk->LVBlen);
   if( lk->LVB == NULL ) {
      fprintf(FP,"LVB            :\n");
   }else{
      fprintf(FP,"LVB            : '%s'\n", lvbtob64(lk->LVB, lk->LVBlen));
   }
   fprintf(FP,"HolderCount    : %d\n", lk->HolderCount);
   
   fprintf(FP,"Holders        :\n");
   print_holder_list(FP, &lk->Holders);

   fprintf(FP,"LVBHolderCount : %d\n", lk->LVB_holder_cnt);
   fprintf(FP,"LVBHolders     :\n");
   print_holder_list(FP, &lk->LVB_holders );

   fprintf(FP,"ExpiredCount   : %d\n", lk->ExpiredCount);
   fprintf(FP,"ExpiredHolders :\n");
   print_holder_list(FP, &lk->ExpHolders );

   if( lk->reply_waiter == NULL ) {
   fprintf(FP,"reply_waiter   :\n");
   }else{
      fprintf(FP, "reply_waiter   :\n");
      print_waiter(FP, lk->reply_waiter);
   }

   fprintf(FP,"Waiters        :\n");
   print_wait_queue(FP, &lk->Waiters);

   fprintf(FP,"High_Waiters   :\n");
   print_wait_queue(FP, &lk->High_Waiters);

   fprintf(FP,"Action_Waiters :\n");
   print_wait_queue(FP, &lk->Action_Waiters);

   fprintf(FP,"State_Waiters  :\n");
   print_wait_queue(FP, &lk->State_Waiters);

#ifdef LOCKHISTORY
   fprintf(FP,"History  :\n");
   print_wait_queue(FP, &lk->History);
#endif
}

/**
 * _dump_locks_ - 
 * @item: 
 * @d: 
 * 
 * 
 * Returns: int
 */
int _dump_locks_(LLi_t *item, void *d)
{
   Lock_t *lk;
   FILE* fp = (FILE*)d;

   lk = LLi_data(item);
   print_lock(fp, lk);
   fprintf(fp, "#=================\n");
   return 0;
}

/**
 * dump_locks - 
 * 
 * open log file.  dump entire locktable out.
 * 
 * Returns: void
 */
void dump_locks(void)
{
   char *c;
   FILE *fp;
   int fd;

   c = malloc(19 + strlen(LTname) +2);
   if( c == NULL ) return;
   strcpy(c, "Gulm_LT_Lock_Dump.LT");
   strcat(c, LTname);
   
   if( (fd=open_tmp_file(c)) < 0 ) return;
   if( (fp = fdopen(fd,"a")) == NULL) {free(c); return;}

   fprintf(fp, "# BEGIN LOCK DUMP\n");

   hash_walk(AllLocks, _dump_locks_, fp);

   fprintf(fp, "# END LOCK DUMP\n");
   fprintf(fp, "#======================================="
               "========================================\n");
   fclose(fp);
   free(c);
}

void dump_holders(char *s, LLi_t *list)
{
   LLi_t *tp;
   Holders_t *h;
   if( ! LLi_empty( list ) ) {
      for(tp=LLi_next(list); LLi_data(tp)!= NULL; tp=LLi_next(tp)){
         h = LLi_data(tp);
#ifdef DEBUG
         fprintf(stderr,"EXTRA   %s      name = %s\n", s, h->name);
#else
         syslog(LOG_NOTICE,"EXTRA   %s      name = %s\n", s, h->name);
#endif
      }
   }
}

/**
 * send_stats - 
 * @enc: 
 * 
 * The reply code and list start are sent before this function is called.
 * And the list stop will be sent when after this returns.
 * 
 * Returns: int
 */
int send_stats(xdr_enc_t *enc)
{
   int err;
   char tmp[256];

   if((err = xdr_enc_string(enc, "exclusive")) != 0) return err;
   snprintf(tmp, 256, "%lu", cnt_exl_holds);
   if((err = xdr_enc_string(enc, tmp)) != 0) return err;

   if((err = xdr_enc_string(enc, "shared")) != 0) return err;
   snprintf(tmp, 256, "%lu", cnt_shd_holds);
   if((err = xdr_enc_string(enc, tmp)) != 0) return err;

   if((err = xdr_enc_string(enc, "deferred")) != 0) return err;
   snprintf(tmp, 256, "%lu", cnt_dfr_holds);
   if((err = xdr_enc_string(enc, tmp)) != 0) return err;

   if((err = xdr_enc_string(enc, "lvbs")) != 0) return err;
   snprintf(tmp, 256, "%lu", cnt_lvb_holds);
   if((err = xdr_enc_string(enc, tmp)) != 0) return err;

   if((err = xdr_enc_string(enc, "expired")) != 0) return err;
   snprintf(tmp, 256, "%lu", cnt_exp_holds);
   if((err = xdr_enc_string(enc, tmp)) != 0) return err;

   if((err = xdr_enc_string(enc, "lock ops")) != 0) return err;
   snprintf(tmp, 256, "%lu", cur_lops);
   if((err = xdr_enc_string(enc, tmp)) != 0) return err;

   if((err = xdr_enc_string(enc, "conflicts")) != 0) return err;
   snprintf(tmp, 256, "%lu", cnt_conflicts);
   if((err = xdr_enc_string(enc, tmp)) != 0) return err;

   if((err = xdr_enc_string(enc, "incomming_queue")) != 0) return err;
   snprintf(tmp, 256, "%lu", cnt_inq);
   if((err = xdr_enc_string(enc, tmp)) != 0) return err;

   if((err = xdr_enc_string(enc, "conflict_queue")) != 0) return err;
   snprintf(tmp, 256, "%lu", cnt_confq);
   if((err = xdr_enc_string(enc, tmp)) != 0) return err;

   if((err = xdr_enc_string(enc, "reply_queue")) != 0) return err;
   snprintf(tmp, 256, "%lu", cnt_replyq);
   if((err = xdr_enc_string(enc, tmp)) != 0) return err;

   if((err = xdr_enc_string(enc, "used_locks")) != 0) return err;
   snprintf(tmp, 256, "%lu", cnt_locks);
   if((err = xdr_enc_string(enc, tmp)) != 0) return err;

   if((err = xdr_enc_string(enc, "free_locks")) != 0) return err;
   snprintf(tmp, 256, "%lu", free_locks);
   if((err = xdr_enc_string(enc, tmp)) != 0) return err;

   if((err = xdr_enc_string(enc, "free_lkrqs")) != 0) return err;
   snprintf(tmp, 256, "%lu", free_lkrqs);
   if((err = xdr_enc_string(enc, tmp)) != 0) return err;

   if((err = xdr_enc_string(enc, "used_lkrqs")) != 0) return err;
   snprintf(tmp, 256, "%lu", used_lkrqs);
   if((err = xdr_enc_string(enc, tmp)) != 0) return err;

   if((err = xdr_enc_string(enc, "free_holders")) != 0) return err;
   snprintf(tmp, 256, "%lu", free_holders);
   if((err = xdr_enc_string(enc, tmp)) != 0) return err;

   if((err = xdr_enc_string(enc, "used_holders")) != 0) return err;
   snprintf(tmp, 256, "%lu", used_holders);
   if((err = xdr_enc_string(enc, tmp)) != 0) return err;

   if((err = xdr_enc_string(enc, "highwater")) != 0) return err;
   snprintf(tmp, 256, "%lu", max_locks);
   if((err = xdr_enc_string(enc, tmp)) != 0) return err;

   return 0;
}

/*****************************************************************************/
/**
 * count_list - 
 * @l: 
 * 
 * count how many items there are in a LLi list.
 * Needed so I can add some checks to try and find out when the list and
 * the counters I maintain get out of sync.
 * 
 * Returns: int
 */
int count_list(LLi_t *l)
{
   LLi_t *tp;
   int cnt=0;
   for(tp=LLi_next(l); LLi_data(tp) != NULL; tp=LLi_next(tp)) cnt++;
   return cnt;
}
/*****************************************************************************/
/* there are three lists that are made of Holders_t elements.  Here is one
 * set of base functions to manipulate those lists, with some thin wrappers
 * to twist the other bits. (counts, and lvb buffer.)
 */

/**
 * get_new_holder - 
 * 
 * Returns: Holders_t
 */
Holders_t *get_new_holder(void)
{
   Holders_t *h;
   if( LLi_empty( &Free_Holders ) ) {
      h = malloc(sizeof(Holders_t));
   }else{
      LLi_t *tmp;
      tmp = LLi_pop( &Free_Holders );
      LLi_unhook( tmp );
      h = LLi_data(tmp);
      free_holders --;
   }
   if( h == NULL ) return NULL;
   used_holders ++;
   return h;
}

/**
 * recycle_holder - 
 * @h: 
 */
void recycle_holder(Holders_t *h)
{
   LLi_t *tp;
   if( h->name != NULL ) {free(h->name); h->name = NULL;}
   LLi_unhook(&h->cl_list);
   LLi_add_before( &Free_Holders, &h->cl_list );
   used_holders --;
   free_holders ++;
   /* only keep a limited number of free holders around */
   if(free_holders > gulm_config.lt_preholds) {
      tp = LLi_prev(&Free_Holders);
      LLi_del(tp);
      h = LLi_data(tp);
      free(h);
      free_holders --;
   }
}

/**
 * duplicate_holder - 
 * @old: 
 * 
 * 
 * Returns: Holders_t
 */
Holders_t *duplicate_holder(Holders_t *old)
{
   Holders_t *new;
   new = get_new_holder();
   if( new == NULL ) return NULL;
   LLi_init( &new->cl_list, new );
   new->name = strdup(old->name);
   if( new->name == NULL ) { recycle_holder(new); return NULL; }
   new->subid = old->subid;
   new->state = old->state;
   new->start = old->start;
   new->stop = old->stop;
   new->flags = old->flags;
   new->idx = old->idx;
   return new;
}

/**
 * compare_names_subids - 
 * @nA: 
 * @sA: 
 * @nB: 
 * @sB: 
 * 
 * compares the names, including the optional subid
 *
 * if names are equal
 * and
 *  subids are NULL
 *  or
 *   both subids are not null
 *   and
 *   subids match
 * 
 * Returns: TRUE if equal, FALSE if not
 */
int __inline__ compare_names_subids(uint8_t *nA, uint64_t sA, uint8_t *nB, uint64_t sB)
{
   return ( strcmp(nA, nB) == 0 && sA == sB );
}
int __inline__ compare_holder_waiter_names(Holders_t *h, Waiters_t *w)
{
   return compare_names_subids(h->name, h->subid, w->name, w->subid);
}
int __inline__ compare_waiter_waiter_names(Waiters_t *h, Waiters_t *w)
{
   return compare_names_subids(h->name, h->subid, w->name, w->subid);
}
int __inline__ compare_holder_holder_names(Holders_t *h, Holders_t *w)
{
   return compare_names_subids(h->name, h->subid, w->name, w->subid);
}

/**
 * add_holder_to_list - 
 * @name: 
 * @list: 
 * 
 * 
 * Returns: 0:Ok, <0:Error;
 */
int add_holder_to_list(uint8_t *name, LLi_t *list)
{
   Holders_t *h;

   h = get_new_holder();
   if( h == NULL ) return -1;

   LLi_init( &h->cl_list, h );
   h->name = strdup(name);
   h->idx = 0;
   if( h->name == NULL ) {
      recycle_holder(h);
      return -1;
   }

   LLi_add_after( list, &h->cl_list);
   return 0;
}

/**
 * have_holder_in_list - 
 * @name: 
 * @list: 
 * 
 * 
 * Returns: TRUE or FALSE
 */
int have_holder_in_list(uint8_t *name, LLi_t *list)
{
   LLi_t *tp;
   Holders_t *h;

   if( ! LLi_empty( list ) ) {
      for(tp=LLi_next(list); LLi_data(tp) != NULL; tp=LLi_next(tp)) {
         h = LLi_data(tp);
         if( strcmp(h->name, name) == 0 ) {
            return TRUE;
         }
      }
   }
   return FALSE;
}

/**
 * remove_holder_from_list - 
 * @name: 
 * @list: 
 * 
 * returns TRUE if name was in list.
 * 
 * Returns: TRUE or FALSE
 */
int remove_holder_from_list(uint8_t *name, LLi_t *list)
{
   LLi_t *tp;
   Holders_t *h;

   if( ! LLi_empty( list ) ) {
      for(tp=LLi_next(list); LLi_data(tp) != NULL; tp=LLi_next(tp)) {
         h = LLi_data(tp);
         if( strcmp(h->name, name) == 0 ) {
            LLi_del(tp);
            recycle_holder(h);
            return TRUE;
         }
      }
   }
   return FALSE;
}

/**
 * delete_entire_holder_list - 
 * @list: 
 */
void delete_entire_holder_list(LLi_t *list)
{
   Holders_t *h;
   while( ! LLi_empty( list ) ) {
      h = LLi_data( LLi_next( list ) );
      LLi_del( LLi_next( list ) );
      recycle_holder(h);
   }
}

/*****************************************************************************/
/**
 * increment_global_state_counters - 
 * @state: 
 * 
 * 
 * Returns: void
 */
void increment_global_state_counters(int state)
{
   switch(state) {
      case gio_lck_st_Exclusive: cnt_exl_holds++; break;
      case gio_lck_st_Deferred: cnt_dfr_holds++; break;
      case gio_lck_st_Shared: cnt_shd_holds++; break;
   }
}
/**
 * decrement_global_state_counters - 
 * @state: 
 * 
 * 
 * Returns: void
 */
void decrement_global_state_counters(int state)
{
   switch(state) {
      case gio_lck_st_Exclusive: cnt_exl_holds--; break;
      case gio_lck_st_Deferred: cnt_dfr_holds--; break;
      case gio_lck_st_Shared: cnt_shd_holds--; break;
   }
}
/*****************************************************************************/


/**
 * add_to_holders - 
 * @lk: 
 * @lkrq: 
 * 
 * 
 * Returns: int
 */
int add_to_holders(Lock_t *lk, Waiters_t *lkrq)
{
   Holders_t *h;

   h = get_new_holder();
   if( h == NULL ) return -1;
   LLi_init( &h->cl_list, h );
   h->name = strdup(lkrq->name);
   if( h->name == NULL ) {
      recycle_holder(h);
      return -1;
   }
   h->subid = lkrq->subid;
   h->idx = lkrq->idx;
   h->state = lkrq->state;
   h->start = lkrq->start;
   h->stop = lkrq->stop;
   h->flags = lkrq->flags;

   increment_global_state_counters(h->state);

   LLi_add_after( &lk->Holders, &h->cl_list);
   lk->HolderCount++;
   return 0;
}

/**
 * check_for_holder - 
 * @lk: 
 * @lkrq: 
 * 
 * 
 * Returns: int
 */
int check_for_holder(Lock_t *lk, Waiters_t *lkrq)
{
   LLi_t *tp;
   Holders_t *h;

   if( ! LLi_empty( &lk->Holders ) ) {
      for(tp=LLi_next(&lk->Holders); LLi_data(tp) != NULL; tp=LLi_next(tp)) {
         h = LLi_data(tp);
         if( compare_holder_waiter_names(h, lkrq) ) {
            return TRUE;
         }
      }
   }
   return FALSE;
}

/**
 * drop_holder - 
 * @lk: 
 * @lkrq: 
 * 
 * 
 * Returns: int
 */
int drop_holder(Lock_t *lk, Waiters_t *lkrq)
{
   LLi_t *tp;
   Holders_t *h;

   if( ! LLi_empty( &lk->Holders ) ) {
      for(tp=LLi_next(&lk->Holders); LLi_data(tp) != NULL; tp=LLi_next(tp)) {
         h = LLi_data(tp);
         if( compare_holder_waiter_names(h, lkrq) ) {
            LLi_del(tp);
            lk->HolderCount--;
            decrement_global_state_counters(h->state);
            recycle_holder(h);
            return 0;
         }
      }
   }
   return -1;
}


/*****************************************************************************/
/**
 * move_to_Expholders - 
 * @h: 
 * @lk: 
 * 
 * Move Holder h, to the Expired list.
 * 
 * Returns: int
 */
int move_to_Expholders(Holders_t *h, Lock_t *lk)
{
   LLi_unhook(&h->cl_list);
   LLi_add_after( &lk->ExpHolders, &h->cl_list );
   lk->ExpiredCount++;
   cnt_exp_holds ++;
   return 0;
}

/**
 * check_for_expholder - 
 * @hld: 
 * @lk: 
 * 
 * 
 * Returns: int
 */
int check_for_expholder(Holders_t *hld, Lock_t *lk)
{
   LLi_t *tp;
   Holders_t *h;

   if( ! LLi_empty( &lk->ExpHolders ) ) {
      for(tp=LLi_next(&lk->ExpHolders); LLi_data(tp) != NULL; tp=LLi_next(tp)) {
         h = LLi_data(tp);
         if( compare_holder_holder_names(h, hld) ) {
            return TRUE;
         }
      }
   }
   return FALSE;
}

/**
 * drop_expholders - 
 * @name: 
 * @lk: 
 * 
 * This drops every exp holder with matching name.
 * subids are ignored here.
 * 
 * Returns: int
 */
int drop_expholders(uint8_t *name, Lock_t *lk)
{
   LLi_t *tmp,*next;
   Holders_t *h;
   int ret = FALSE;

   for(tmp = LLi_next(&lk->ExpHolders); LLi_data(tmp) != NULL; tmp = next) {
      next = LLi_next(tmp);
      h = LLi_data(tmp);
      if( strcmp(name, h->name) == 0) {
         LLi_del(tmp);
         recycle_holder(h);
         lk->ExpiredCount--;
         cnt_exp_holds --;
         ret = TRUE;
      }
   }
   return ret;
}

/*****************************************************************************/
/**
 * add_to_LVB_holders - 
 * @name: 
 * @lk: 
 * 
 * 
 * Returns: int
 */
int add_to_LVB_holders(uint8_t *name, Lock_t *lk)
{
#define FIRST_LVB_SIZE (32) /* gotta start somewhere. */
   lvb_log_msg("Adding %s to LVB holders for lock (%s)\n",
         name, lkeytob64(lk->key, lk->keylen));

   if( add_holder_to_list(name, &lk->LVB_holders) != 0 )
      return -1;

   lk->LVB_holder_cnt++;
   cnt_lvb_holds++;
   if( lk->LVB_holder_cnt == 1 ) { /* first lvb hold */
      lk->LVBlen = FIRST_LVB_SIZE;
      if( lk->LVB == NULL ) {
         lk->LVB = malloc(FIRST_LVB_SIZE);
         if( lk->LVB == NULL ) die(ExitGulm_NoMemory, "No memory left.");
      }
      memset( lk->LVB, 0, FIRST_LVB_SIZE);
      lvb_log_msg( "Zeroing LVB (%s) First holder, for %s\n",
            lkeytob64(lk->key, lk->keylen), name);
   }
   return 0;
#undef FIRST_LVB_SIZE
}

/**
 * check_for_LVB_holder - 
 * @name: 
 * @lk: 
 * 
 * 
 * Returns: TRUE or FALSE
 */
int __inline__ check_for_LVB_holder(uint8_t *name, Lock_t *lk)
{ return have_holder_in_list(name, &lk->LVB_holders); }

/**
 * drop_LVB_holder - 
 * @name: 
 * @lk: 
 * 
 * 
 * Returns: int
 */
int drop_LVB_holder(uint8_t *name, Lock_t *lk)
{
   lvb_log_msg("Dropping %s from LVB holders for lock (%s)\n",
         name, lkeytob64(lk->key, lk->keylen));

   if(remove_holder_from_list(name, &lk->LVB_holders)) {
      lk->LVB_holder_cnt--;
      cnt_lvb_holds--;
      if( lk->LVB_holder_cnt == 0) {
         lk->LVBlen = 0; /* no more lvb holders */
         if( lk->LVB != NULL ) {free(lk->LVB); lk->LVB = NULL;}
      }
      return 0;
   }
   return -1;
}

/**
 * lvbcpy - 
 * @lk: 
 * @lkrq: 
 * 
 * This copies a LVB into a Lock struct from the request struct.  Since
 * this action could cause the resizing of the LVB in the lock struct, we
 * need to make sure of a few things.
 * 
 */
void lvbcpy(Lock_t *lk, Waiters_t *lkrq)
{
   if( lk->LVB == NULL || lkrq->LVB == NULL || lkrq->LVBlen == 0 ) {
      /* skip */
      return;
   }else
   if( lk->LVBlen == lkrq->LVBlen ) {
      memcpy(lk->LVB, lkrq->LVB, lk->LVBlen);
   }else
   if( lk->LVBlen != lkrq->LVBlen ) { /* handle grows or shrinks. */
      uint8_t *c;
      c = realloc(lk->LVB, lkrq->LVBlen);
      if( c == NULL ) {
         die(ExitGulm_NoMemory, "Out of memory in LVB resize\n");
      }
      lk->LVB = c;
      lk->LVBlen = lkrq->LVBlen;
      memcpy(lk->LVB, lkrq->LVB, lk->LVBlen);
   }
   lvb_log_msg("For %s, Lock %s: Saved LVB %s\n",
       lkrq->name, lkeytob64(lk->key,lk->keylen),
       lvbtohex(lk->LVB, lk->LVBlen));
}

/*****************************************************************************/

/**
 * find_lock - 
 * @key: 
 * 
 * Should always return a lock.  If its not in the tables, adds it.
 *
 * This should be designed such that it always works.  (pre-allocate and
 * stuff.) someday, maybe...
 * 
 * Returns: Lock_t
 */
Lock_t *find_lock(uint8_t *key, uint8_t keylen)
{
   LLi_t *tmp;
   Lock_t *lk;
   int ret;

#if 0
   log_bug("Searching for key %s \n", lkeytob64(key));
#endif

   tmp = hash_find(AllLocks, key, keylen);
   if( tmp == NULL ) {
      if( LLi_empty( &Free_lock ) ) {
         lk = malloc(sizeof(Lock_t));
      }else{
         tmp = LLi_pop( &Free_lock );
         LLi_unhook( tmp );
         lk = LLi_data( tmp );
         free_locks --;
      }
      if( lk == NULL )
         die(ExitGulm_NoMemory,
               "Failed to malloc new lock.\n");
      memset(lk, 0, sizeof(Lock_t));
      lk->key = malloc(keylen);
      if( lk->key == NULL )
         die(ExitGulm_NoMemory,
               "Failed to malloc new key.\n");
      memcpy(lk->key, key, keylen);
      lk->keylen = keylen;
      lk->LVBlen = 0;
      lk->LVB = NULL;
      lk->HolderCount = 0;
      lk->ExpiredCount = 0;
      lk->LVB_holder_cnt = 0;
      lk->reply_waiter = NULL;

      LLi_init( &lk->lk_list, lk);
      LLi_init_head( &lk->Holders );
      LLi_init_head( &lk->ExpHolders );
      LLi_init_head( &lk->LVB_holders );
      Qu_init_head( &lk->Waiters );
      Qu_init_head( &lk->High_Waiters );
      Qu_init_head( &lk->Action_Waiters );
      Qu_init_head( &lk->State_Waiters );

#ifdef LOCKHISTORY
      lk->Histlen = 0;
      Qu_init_head( &lk->History );
#endif

      if( (ret=hash_add(AllLocks, &lk->lk_list)) <0) {
         dump_locks();
         die(ExitGulm_NoMemory,
               "Failed to add new key(%s). Should never happen.(%d)\n",
               lkeytob64(key, keylen), ret);
      }
      cnt_locks++;
   } else {
      lk = LLi_data(tmp);
   }

   return lk;
}

/**
 * check_for_recycle - if no refs left, free.
 * @lk: 
 * 
 * It is quite wise to run the wait Qu **before** you call this.
 * 
 * Returns: void
 */
void check_for_recycle(Lock_t *lk)
{
   LLi_t *temp;
   Lock_t *chck;

   GULMD_ASSERT( lk != NULL , );
   GULMD_ASSERT( lk->key != NULL , );

   if( lk->HolderCount == 0 &&
       lk->ExpiredCount == 0 &&
       lk->LVB_holder_cnt == 0 &&
       Qu_empty( &lk->Waiters ) &&
       Qu_empty( &lk->High_Waiters ) &&
       Qu_empty( &lk->Action_Waiters ) &&
       Qu_empty( &lk->State_Waiters ) &&
#ifdef LOCKHISTORY
       Qu_empty( &lk->History ) &&
#endif
       lk->reply_waiter == NULL )
   {
      /* Should I die here? Or should I print out warnings, and just
       * stop the recycle?
       * If these fail, you have memory problems. we will die.
       */

      dump_holders("Holder", &lk->Holders);
      dump_holders("ExpHolder", &lk->ExpHolders);
      dump_holders("LVBHolder", &lk->LVB_holders);

      GULMD_ASSERT( LLi_empty( &lk->Holders ),
            log_msg(lgm_Always, "lk: %s\n", lkeytob64(lk->key, lk->keylen));
            dump_locks();
            );
      GULMD_ASSERT( LLi_empty( &lk->ExpHolders ),
            log_msg(lgm_Always, "lk: %s\n", lkeytob64(lk->key, lk->keylen));
            dump_locks();
            );
      GULMD_ASSERT( LLi_empty( &lk->LVB_holders ),
            log_msg(lgm_Always, "lk: %s\n", lkeytob64(lk->key, lk->keylen));
            dump_locks();
            );

      temp = hash_del(AllLocks, lk->key, lk->keylen);
      chck = LLi_data(temp);
      GULMD_ASSERT( chck == lk, );

      /* everything is as it should be. */
      if( lk->LVB ) {free( lk->LVB ); lk->LVB = NULL; }
      free(lk->key);
      lk->key = NULL;
      /* stick it onto the free lock list */
      LLi_unhook( &lk->lk_list );
      LLi_add_before( &Free_lock, &lk->lk_list );
      cnt_locks--;
      free_locks ++;

      /* only keep a limited number of free structs around. */
      if(free_locks > gulm_config.lt_prelocks) {
         temp = LLi_prev(&Free_lock);
         LLi_del(temp);
         chck = LLi_data(temp);
         free(chck);
         free_locks --;
      }
   }
}

/**
 * get_new_lkrq - 
 * 
 * 
 * Returns: Waiter_t
 */
Waiters_t *get_new_lkrq(void)
{
   Waiters_t *lkrq;
   LLi_t *tmp;
   if( LLi_empty(&Free_lkrq) ) {
      lkrq = malloc(sizeof(Waiters_t));
      if( lkrq == NULL ) return NULL;
   }else{
      tmp = LLi_pop(&Free_lkrq);
      LLi_unhook( tmp );
      lkrq = LLi_data(tmp);
      free_lkrqs --;
   }
   used_lkrqs ++;
   memset(lkrq, 0, sizeof(Waiters_t)); /* HAS to be 0 !!! */
   LLi_init( &lkrq->wt_list, lkrq);
   LLi_init_head( &lkrq->holders );
   lkrq->idx = -1;
#ifdef LOCKHISTORY
   {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      lkrq->starttime = tvs2uint64(tv);
   }
#endif

   return lkrq;
}

/**
 * recycle_lkrq - 
 * @lkrq: 
 */
void recycle_lkrq(Waiters_t *lkrq)
{
   LLi_unhook( &lkrq->wt_list );
   if( lkrq->name != NULL ) {free(lkrq->name);lkrq->name = NULL;}
   if( lkrq->key != NULL ) {free(lkrq->key);lkrq->key = NULL;}
   lkrq->keylen = 0;
   if( lkrq->LVB != NULL ) {free(lkrq->LVB);lkrq->LVB = NULL;}
   lkrq->LVBlen = 0;
   lkrq->idx = -1;

   delete_entire_holder_list(&lkrq->holders);

   LLi_add_before( &Free_lkrq, &lkrq->wt_list );
   used_lkrqs --;
   free_lkrqs ++;

   /* don't keep too many free structs around. */
   if( free_lkrqs > gulm_config.lt_prelkrqs  ) {
      LLi_t *tmp;
      Waiters_t *l;
      tmp = LLi_prev(&Free_lkrq);
      LLi_del(tmp);
      l = LLi_data(tmp);
      free(l);
      free_lkrqs --;
   }
}

/**
 * duplicate_lkrw - 
 * @old: 
 * 
 * creates a new chunk of memory that is an excate copy of the lkrq passed
 * in.
 * 
 * Because sometimes, you need to take both forks in the road.
 *
 * Returns: Waiters_t
 */
Waiters_t *duplicate_lkrw(Waiters_t *old)
{
   Waiters_t *new;
   new = get_new_lkrq();
   if( new == NULL ) return NULL;
   new->name = strdup( old->name );
   if( new->name == NULL ) goto fail;
   new->key = malloc(old->keylen);
   if( new->key == NULL ) goto fail;
   memcpy(new->key, old->key, old->keylen);
   new->keylen = old->keylen;
   new->subid = old->subid;
   new->op = old->op;
   new->state = old->state;
   new->start = old->start;
   new->stop = old->stop;
   new->flags = old->flags;
   new->LVB = malloc(old->LVBlen);
   if( new->LVB == NULL ) goto fail;
   memcpy(new->LVB, old->LVB, old->LVBlen);
   new->LVBlen = old->LVBlen;
   new->Slave_rpls = old->Slave_rpls;
   new->Slave_sent = old->Slave_sent;
   new->idx = old->idx;
   new->ret = old->ret;

   /* we don't dup holders right now.  Current usage of the holders on lkrq
    * doesn't need it.  If things need it later, it can be added then.
    * (get_new_lkrq() initialized that field to empty, so we can just leave
    * it.)
    */

   return new;
fail:
   recycle_lkrq(new);
   return NULL;
}

#ifdef LOCKHISTORY
/**
 * record_lkrq - 
 * @lk: 
 * @lkrq: 
 * 
 * record this lock req in this lock's history.
 * typically, this should get called instead of recycle_lkrq() when you
 * want to save lock history.
 *
 * Arranged, so that the dumps show the most current request at the top of
 * the dump.
 * 
 * Returns: void
 */
void record_lkrq(Lock_t *lk, Waiters_t *lkrq)
{
   struct timeval tv;
   gettimeofday(&tv, NULL);

   LLi_unhook( &lkrq->wt_list );
   lkrq->stoptime = tvs2uint64(tv);
   LLi_add_after( &lk->History, &lkrq->wt_list );
   lk->Histlen ++;

   /* keep it from getting too long */
   if( lk->Histlen > LOCKHISTORY ) {
      LLi_t *tmp;
      Waiters_t *w;

      tmp = LLi_prev( &lk->History );
      LLi_del(tmp);
      LLi_unhook(tmp);
      lk->Histlen --;
      w = LLi_data(tmp);
      recycle_lkrq(w);
   }
}
#endif /*LOCKHISTORY*/

/**
 * delete_entire_waiters_list - 
 * @q: 
 * 
 * blindly wipes out a waiters queue.
 *
 */
void delete_entire_waiters_list( Qu_t *q)
{
   Waiters_t *w;
   while( !Qu_empty( q ) ) {
      w = Qu_data( Qu_DeQu( q ) );
      recycle_lkrq(w);
   }
}

/**
 * reset_lockstruct - 
 * @lk: 
 * 
 * makes an existing lock struct look like a freshly allocated one.
 * basically clear everything except the key.
 * 
 */
void reset_lockstruct(Lock_t *lk)
{
   lk->LVBlen = 0;
   if( lk->LVB != NULL ) {free(lk->LVB);lk->LVB=NULL;}
   lk->HolderCount    = 0;
   lk->ExpiredCount   = 0;
   lk->LVB_holder_cnt = 0;
   
   delete_entire_holder_list( &lk->Holders );
   delete_entire_holder_list( &lk->ExpHolders );
   delete_entire_holder_list( &lk->LVB_holders );

   delete_entire_waiters_list( &lk->Waiters );
   delete_entire_waiters_list( &lk->High_Waiters );
   delete_entire_waiters_list( &lk->Action_Waiters );
   delete_entire_waiters_list( &lk->State_Waiters );

#ifdef LOCKHISTORY
   lk->Histlen = 0;
   delete_entire_waiters_list( &lk->History );
#endif

   if( lk->reply_waiter != NULL ) {
      recycle_lkrq(lk->reply_waiter);
      lk->reply_waiter = NULL;
   }

}

/**
 * _clear_lockspace_ - 
 * @item: 
 * @misc: 
 * 
 * delete a lock using previously written functions.
 * 
 * Returns: int
 */
int _clear_lockspace_(LLi_t *item, void *misc)
{
   Lock_t * lk = LLi_data(item);
   reset_lockstruct(lk);
   check_for_recycle(lk);
   return 0;
}

/**
 * clear_lockspace - 
 * 
 * wipe out all locks and reset counters and such.
 * 
 */
void clear_lockspace(void)
{
   /* empty lock space. */
   hash_walk(AllLocks, _clear_lockspace_, NULL);

   /* reset counters. */
   cnt_locks     = 0;
   cnt_exl_holds = 0;
   cnt_shd_holds = 0;
   cnt_dfr_holds = 0;
   cnt_lvb_holds = 0;
   cnt_exp_holds = 0;
   cnt_inq       = 0;
   cnt_confq     = 0;
   cnt_replyq    = 0;
}

/**
 * check_fullness - 
 */
void check_fullness(void)
{
   static unsigned long last_secs = 0;
   struct timeval tv;
   if( cnt_locks > max_locks ) {
      gettimeofday(&tv, NULL);
      if( last_secs + gulm_config.lt_cf_rate < tv.tv_sec ) {
         log_msg(lgm_Always,
               "Lock count is at %ld which is more than the max %ld. "
               "Sending Drop all req to clients\n", cnt_locks, max_locks);
         send_drop_all_req();
         last_secs = tv.tv_sec;
      }
   }
}

/*****************************************************************************/
/**
 * drop_holder_by_range - 
 * @lk: 
 * @lkrq: 
 * 
 * For every holder that matches name, shrink/split/drop
 *
 * Note that the order of the range check below is important.  Certain
 * cases are know not to exist as things fall down the checks.  (for
 * example, we know that by the time we get the the shrink operations, that
 * we won't be setting a holder to have a start after its stop.  Because if
 * it was that close, it got matched above by a drop.)
 *
 * This may not be the most efficent impementation, but we can always fix
 * that later.
 * 
 * Returns: int
 */
int drop_holder_by_range(Lock_t *lk, Waiters_t *lkrq)
{
   LLi_t *tp,*nxt;
   Holders_t *h;
   int ret = -1;

   if( ! LLi_empty( &lk->Holders ) ) {
      for(tp=LLi_next(&lk->Holders); LLi_data(tp) != NULL; tp = nxt ) {
         nxt = LLi_next(tp);
         h = LLi_data(tp);
         if( compare_holder_waiter_names(h, lkrq) ) {
            /* alright, matching name. now, does it over lap? */

            /* Drop holder */
            /* |-- lkrq --|
             *   |- h -|
             */
            if( lkrq->start <  h->start &&
                lkrq->start <  h->stop  &&
                lkrq->stop  >  h->start &&
                lkrq->stop  >  h->stop ) {
               LLi_del(tp);
               lk->HolderCount--;
               decrement_global_state_counters(h->state);
               recycle_holder(h);
               ret = 0;
            }else
            /* |- lkrq -|
             * |-- h ---|
             */
            if( lkrq->start == h->start &&
                lkrq->stop  == h->stop ) {
               LLi_del(tp);
               lk->HolderCount--;
               decrement_global_state_counters(h->state);
               recycle_holder(h);
               ret = 0;
            }else
            /* |- lkrq -|
             *    |- h -|
             */
            if( lkrq->start <  h->start &&
                lkrq->start <  h->stop  &&
                lkrq->stop  >  h->start &&
                lkrq->stop  == h->stop ) {
               LLi_del(tp);
               lk->HolderCount--;
               decrement_global_state_counters(h->state);
               recycle_holder(h);
               ret = 0;
            }else
            /* |- lkrq -|
             * |- h -|
             */
            if( lkrq->start == h->start &&
                lkrq->start <  h->stop  &&
                lkrq->stop  >  h->start &&
                lkrq->stop  >  h->stop ) {
               LLi_del(tp);
               lk->HolderCount--;
               decrement_global_state_counters(h->state);
               recycle_holder(h);
               ret = 0;
            }else

            /* Shrink holder */
            /* |-- lkrq --|
             * |---------- h -|
             */
            /* |-- lkrq --|
             *          |- h -|
             */
            if( lkrq->start <= h->start &&
                lkrq->start <  h->stop  &&
                lkrq->stop  >  h->start &&
                lkrq->stop  <  h->stop ) {
               h->start = lkrq->stop + 1;
               ret = 0;
            }else
            /*  |-- lkrq --|
             * |- h -------|
             */
            /*  |-- lkrq --|
             * |- h -|
             */
            if( lkrq->start >  h->start &&
                lkrq->start <  h->stop  &&
                lkrq->stop  >  h->start &&
                lkrq->stop  >= h->stop ) {
               h->stop = lkrq->start - 1;
               ret = 0;
            }else

            /* Split Holder */
            /*   |- lkrq -|
             * |----- h -----|
             *  N           O
             */
            if( lkrq->start >  h->start &&
                lkrq->start <  h->stop  &&
                lkrq->stop  >  h->start &&
                lkrq->stop  <  h->stop ) {
               Holders_t *new;
               new = get_new_holder();
               LLi_init( &new->cl_list, new);
               new->name = strdup(h->name);
               GULMD_ASSERT(new->name != NULL, );
               new->subid = h->subid;
               new->idx = h->idx;
               new->state = h->state;
               new->flags = h->flags;
               new->start = h->start;
               new->stop = lkrq->start - 1;

               increment_global_state_counters(new->state);
               LLi_add_before( &h->cl_list, &new->cl_list);
               lk->HolderCount++;

               h->start = lkrq->stop + 1;
               ret = 0;
            }

            /* No overlap */
            /* |-- lkrq --|
             *              |- h -|
             */
            /*         |-- lkrq --|
             * |- h -|
             */
            /* Don't need to actually match these, just let things cycle to
             * the next in the list.
             */

         }/*compare_holder_waiter_names(h, lkrq)*/
      }/*for()*/
   }/*! LLi_empty( &lk->Holders )*/
   return ret;
}

/**
 * conflict_queue_empty - 
 * @lk: 
 * 
 * 
 * Returns: TRUE or FALSE
 */
int __inline__ conflict_queue_empty(Lock_t *lk)
{
   return Qu_empty(&lk->Waiters) && Qu_empty(&lk->High_Waiters);
}

/* State conflict table.
 * is A compatible with B?
 * (sct[A] >> B) & 0x1
 */
uint32_t state_conflict_table[] = {
   /* unlock */ 0xffffffff,  /* unlock is compatible with everything. */
   /* exl    */ 0x00000000,  /* not compat with anyone. */
   /* shr    */ 0x00000004,  /* only with other shares */
   /* dfr    */ 0x00000008   /* only with other defers */
};

/**
 * Do_Holder_Waiter_conflict - 
 * @h: 
 * @w: 
 * 
 * 
 * Returns: TRUE if conflict, FALSE if compatible.
 */
int Do_Holder_Waiter_conflict(Holders_t *h, Waiters_t *w)
{
   /* if ranges do not over lap, no conflict. */
   if( h->start > w->stop || h->stop < w->start ) return FALSE;

   /* if overlap, and same name, no conflict */ /* its a xmot */
   if( compare_holder_waiter_names(h,w) ) return FALSE;

   /* if overlap, different names, but compatible states, no conflict */
   if( (state_conflict_table[h->state] >> w->state) & 0x1 ) return FALSE;

   return TRUE;
}

/**
 * check_for_conflict - 
 * @lk: 
 * @lkrq: 
 * 
 * does this lock request conflist with any existing holders?
 *
 * stop after first found. Only need one conflict for this check.
 * 
 * Returns: TRUE if conflict, FALSE if compatible.
 */
int check_for_conflict(Lock_t *lk, Waiters_t *lkrq)
{
   LLi_t *tp;
   Holders_t *h;

   if( !LLi_empty(&lk->Holders) ) {
      for(tp = LLi_next(&lk->Holders);
          NULL != LLi_data(tp);
          tp = LLi_next(tp)) {
         h = LLi_data(tp);
         if( Do_Holder_Waiter_conflict(h,lkrq) ) return TRUE;
      }
   }

   return FALSE; /* no conflicts. */
}

/**
 * send_lock_success - 
 * @lk: 
 * @lkrq: 
 * 
 * 
 * Returns: void
 */
int send_lock_success(Lock_t *lk, Waiters_t *lkrq)
{
   if( gulm_config.fog ) {
      lk->reply_waiter = lkrq;
      cnt_replyq ++;
      send_req_update_to_slaves(lkrq);
      return 1; /* gotta wait for the reply_waiter to get flushed. */
   }else{
      send_req_lk_reply(lkrq, lk, gio_Err_Ok);
   }
   return 0;
}

/**
 * send_Try_Failed - 
 * @lk: 
 * @lkrq: 
 * 
 * 
 * Returns: void
 */
void send_Try_Failed(Lock_t *lk, Waiters_t *lkrq)
{
   lkrq->flags &= ~gio_lck_fg_Cachable;
   if( lkrq->flags & gio_lck_fg_Do_CB )
      send_drp_req(lk, lkrq);
   send_req_lk_reply(lkrq, lk, gio_Err_TryFailed);
}

/**
 * put_onto_conflict_queue - 
 * @lk: 
 * @lkrq: 
 * 
 * 
 * Returns: void
 */
void put_onto_conflict_queue(Lock_t *lk, Waiters_t *lkrq)
{
   if( lkrq->flags & gio_lck_fg_NoExp || lkrq->flags & gio_lck_fg_Piority ) {
      Qu_EnQu(&lk->High_Waiters, &lkrq->wt_list);
   }else{
      Qu_EnQu(&lk->Waiters, &lkrq->wt_list);
   }
   cnt_confq ++;
   cnt_conflicts ++;
}

/**
 * requeue_conflict - 
 * @lk: 
 * @lkrq: 
 * 
 * 
 * Returns: void
 */
void requeue_conflict(Lock_t *lk, Waiters_t *lkrq)
{
   /* push back onto front. We'll try again later. */
   if((lkrq->flags & gio_lck_fg_Piority) || (lkrq->flags & gio_lck_fg_NoExp)){
      Qu_EnQu_Front( &lk->High_Waiters, &lkrq->wt_list );
   }else{
      Qu_EnQu_Front( &lk->Waiters, &lkrq->wt_list );
   }
   cnt_confq++;
   /* Next lock on Qu is incompatible.
    * Send a drop request to the current holder.
    */
   /* send drop reqs may have to change to work with ranges. XXX */
   send_drp_req(lk, lkrq);
}

/**
 * check_for_any_flag - 
 * @lk: 
 * @lkrq: 
 *
 * Handle the Any flag.  Very much a GFS-ism.
 *
 * this may have to change to work with ranges. XXX
 *
 * Jon had a neat idea for something that would replace the anyflag.
 * Basically, lock states/modes are now a bit field.  And you can ask for
 * multiple modes at once.  What it means is 'give me the best mode of the
 * ones I asked for'  So any would be replaced with (shr|dfr), more
 * interesting is the cases where things like (exl|shr|dfr).  And even more
 * so if more states are added.
 *
 * Implementing this idea would replace this funtion.  The whole concept of
 * the any flag would get dropped. (maybe kept in libgulm, which would just
 * change it to this.)
 *
 * I think the code in check_for_conflict() would only really need to
 * change.  check_for_conflict() could return either a failure (eveything
 * asked for conflicts.) or the best mode that would let the request
 * succede.
 *
 * damn, if that is all that it is, that would be way cleaner than this
 * icky anyflag code.
 *
 * its not just that.  basicly, conflict check returns a bit field that
 * describes which states are held by all holders of this lock.  (it also
 * returns true/false if the req is compatible.)  there needs to be another
 * function, that takes this bit field and the req states field, and
 * determins which state this req will get.
 *
 * still not too bad.  Only major part is going through everything to
 * change the states from indexes to bits.
 *
 * Not quite sure that doing that is really worth the gain.  I may have to
 * play with some forked code later.
 *
 */
void check_for_any_flag(Lock_t *lk, Waiters_t *lkrq)
{
   Holders_t *h;

   if( lkrq->flags & gio_lck_fg_Any &&
       (lkrq->state == gio_lck_st_Deferred ||
        lkrq->state == gio_lck_st_Shared) ) {
      /* ok, lkrq meets any preqs. does the lock? */
      /* We're taking a short cut here, Assuming if the first holder is Shd
       * or Dfr then all are.
       * If new lock states are added in the future that are compat with
       * Shd or Dfr, then this function will need to be revisited.
       */
      if( lk->HolderCount > 0 &&
          (h=LLi_data(LLi_next(&lk->Holders))) != NULL &&
          (h->state == gio_lck_st_Deferred ||
           h->state == gio_lck_st_Shared) ) {
         /* lock meets preqs too, rewrite the request's state. */
         lkrq->state = h->state;
      }
   }
}

/**
 * lkrq_onto_lock - 
 * @lk: 
 * @lkrq: < the lock req to handle.  Should NOT be on any queues!
 * @incomming: < TRUE if this is called from the incomming Queue.
 * 
 * this is called by both incomming queue runer and conflict queue runner.
 *
 * Returns: =0:Queue Emptied !0:Items still in Queue.
 */
int lkrq_onto_lock(Lock_t *lk, Waiters_t *lkrq, int incomming)
{
   int saveLVB=FALSE, ret=0, singleExl=FALSE;
   Holders_t *h;

   check_for_any_flag(lk, lkrq);

   /* Do I have the Exclusive hold on this lock?
    * knowing this lets us do demotes without going through unlock.
    */
   singleExl = lk->HolderCount == 1 &&
               !LLi_empty(&lk->Holders) &&
               (h=LLi_data(LLi_next(&lk->Holders))) != NULL &&
               h->state == gio_lck_st_Exclusive;

   /* the decision to save the LVB or not needs to be made before we
    * actually change the state of the lock and holders.
    * So 'if we will save this LVB' is decided here, but not done until
    * after this req gets the lock.
    */
   saveLVB = lkrq->state != gio_lck_st_Exclusive &&
             (lkrq->flags & gio_lck_fg_hasLVB) &&
             singleExl &&
             check_for_LVB_holder(lkrq->name, lk);
             /* don't need to cmp name, since if holder is exl and if we
              * get it below, we're obiviously the holder.
              */

   if( lkrq->state == gio_lck_st_Unlock ) {
      /* do unlock */
      ret = drop_holder_by_range(lk, lkrq);
      lkrq->flags &= ~gio_lck_fg_Cachable;
      /* check lvb save */
      if( saveLVB && ret == 0 ) lvbcpy(lk, lkrq);
      ret = send_lock_success(lk, lkrq);
   }else
   if( lk->ExpiredCount > 0 && !(lkrq->flags & gio_lck_fg_NoExp ) ) {
      if( incomming ) {
         put_onto_conflict_queue(lk, lkrq);
      }else 
      {
         requeue_conflict(lk, lkrq);
         ret = 1;
      }
   }else
   if( incomming && !singleExl && ! conflict_queue_empty(lk) &&
       check_for_holder(lk, lkrq) ) {
      /* I hold lock, I want to convert. but others in way. */
      if( lkrq->flags & gio_lck_fg_Try ) {
         send_Try_Failed(lk, lkrq);
      }else
      {
         /* Internal Unlock */
         drop_holder_by_range(lk, lkrq);
         lkrq->flags &= ~gio_lck_fg_Cachable;

         /* then queue me on conflict. */
         put_onto_conflict_queue(lk, lkrq);
      }
   }else
   if( check_for_conflict(lk, lkrq) ) {
      if( lkrq->flags & gio_lck_fg_Try ) {
         send_Try_Failed(lk, lkrq);
      }else
      {
         if( incomming ) {
            put_onto_conflict_queue(lk, lkrq);
         }else
         {
            requeue_conflict(lk, lkrq);
            ret = 1;
         }
      }
   }else
   {
      /* Lazy merging.
       * Basically we don't bother merging at all.  To add a new range
       * holder, we first clearout the area we want to add the new range,
       * then just add the new range.
       *
       * The up side is that the code is a lot cleaner.
       * The down side is that certain range activity will end up with a
       * lot more memory used than if real merges happened.
       */
      if( lk->HolderCount != 0 ) {
         /* maybe stuff to drop. */
         drop_holder_by_range(lk, lkrq);
      }
      add_to_holders(lk, lkrq);

      /* check lvb save */
      if( saveLVB ) lvbcpy(lk, lkrq);

      /* send lock success reply */
      ret = send_lock_success(lk, lkrq);
   }

   return ret;
}

/**
 * check_for_waiter - is foo pending here?
 * @Cname: 
 * @lk: 
 * 
 * 
 * Returns: TRUE or FALSE
 */
int check_for_waiter(Lock_t *lk, Waiters_t *lkrq)
{
   LLi_t *tp;
   Waiters_t *w;

   /* XXX Add Action waiters here? */
   if( ! LLi_empty( &lk->High_Waiters ) ) {
      for(tp=LLi_next(&lk->High_Waiters);LLi_data(tp) != NULL;tp=LLi_next(tp)) {
         w = LLi_data(tp);
         if( compare_waiter_waiter_names(lkrq, w) ) {
            return TRUE;
         }
      }
   }
   if( ! LLi_empty( &lk->Waiters ) ) {
      for(tp=LLi_next(&lk->Waiters); LLi_data(tp) != NULL; tp=LLi_next(tp)) {
         w = LLi_data(tp);
         if( compare_waiter_waiter_names(lkrq, w) ) {
            return TRUE;
         }
      }
   }
   return FALSE;
}

/**
 * inner_cancel_waiting_lkrq - 
 * @list: 
 * @name: 
 * @inexp: 
 *
 * 
 * Returns: int
 */
int inner_cancel_waiting_lkrq(LLi_t *list, Waiters_t *lkrq, Lock_t *lk)
{
   LLi_t *tp, *next;
   Waiters_t *w;
   int found = FALSE;
   if( ! LLi_empty( list ) ) {
      for(tp=LLi_next(list);
          LLi_data(tp) != NULL;
          tp = next)
      {
         next = LLi_next(tp);
         w = LLi_data(tp);
         /* do not cancel unlocks. */
         if( w->state != gio_lck_st_Unlock &&
             compare_waiter_waiter_names(lkrq, w) ) {
            LLi_del(tp);
            send_req_lk_reply(w, lk, gio_Err_Canceled);
            found = TRUE;
         }
      }
   }
   return found;
}

/**
 * cancel_waiting_lkrq - Cancel a pending lock request.
 * @Cname: 
 * @lk: 
 * @inexp: 
 *
 * Returns: TRUE or FALSE
 */
int cancel_waiting_lkrq(Lock_t *lk, Waiters_t *lkrq)
{
   int found=FALSE,A,B,C,D;

   /* we do not support canceling reply_waiters
    * maybe in the future, but not now.
    */

   /* this is done the following because if we chained the function calls
    * together with || things won't behave the way we want it to.
    * (and if you don't know why that would be different, go back to
    * school.)
    */
   A = inner_cancel_waiting_lkrq(&lk->State_Waiters, lkrq, lk);
   if( A ) cnt_inq --;
   B = inner_cancel_waiting_lkrq(&lk->Action_Waiters, lkrq, lk);
   if( B ) cnt_inq --;
   C = inner_cancel_waiting_lkrq(&lk->High_Waiters, lkrq, lk);
   if( C ) cnt_confq --;
   D = inner_cancel_waiting_lkrq(&lk->Waiters, lkrq, lk);
   if( D ) cnt_confq --;
   return found || A || B || C || D;
}

/**
 * Run_Action_Incomming_Queue - 
 * @CurrentQu: 
 * @lk: 
 *
 * Returns: =0:Queue Emptied !0:Items still in Queue.
 */
int Run_Action_Incomming_Queue(Qu_t *CurrentQu, Lock_t *lk)
{
   Qu_t *tmp;
   Waiters_t *lkrq;
   Holders_t *h;
   int err=0;

   while( !Qu_empty( CurrentQu ) ) {
      tmp = Qu_DeQu( CurrentQu );
      lkrq = Qu_data(tmp);

      if( lkrq->state == gio_lck_st_HoldLVB ) {
         if( check_for_LVB_holder(lkrq->name, lk) ) {
            err = gio_Err_Ok; /* or should it be gio_Err_NotAllowed? */
         }else
         if( add_to_LVB_holders(lkrq->name, lk) <0) {
            err = gio_Err_MemoryIssues;
         }else
         {
            err = gio_Err_Ok;
         }
      }else
      if( lkrq->state == gio_lck_st_UnHoldLVB ) {
         drop_LVB_holder(lkrq->name, lk);
         err = gio_Err_Ok;
      }else
      if( lkrq->state == gio_lck_st_SyncLVB ) {
         if( lk->HolderCount == 1 &&
             !LLi_empty(&lk->Holders) &&
             (h=LLi_data(LLi_next(&lk->Holders))) != NULL &&
             h->state == gio_lck_st_Exclusive &&
             lk->LVB != NULL && lkrq->LVB != NULL &&
             check_for_LVB_holder(lkrq->name, lk) ) {
            lvbcpy(lk, lkrq);
            err = gio_Err_Ok;
         }else{
            err = gio_Err_NotAllowed;
            log_msg(lgm_Always,
                  "lk->LVB:%p lkrq->LVB:%p lvbholder:%s holder:%s\n",
                  lk->LVB, lkrq->LVB,
                  check_for_LVB_holder(lkrq->name, lk)?"true":"false",
                  check_for_holder(lk, lkrq)?"true":"false" );
         }
      }else
      {
         log_err("Unknown action:%#x name:%s lock:%s \n", lkrq->state, 
                 lkrq->name, lkeytob64(lkrq->key, lkrq->keylen));
         err = gio_Err_BadStateChg;
      }

      cnt_inq --;
      /* now handle error */
      if( err == gio_Err_Ok ) {
         if( gulm_config.fog ) {
            lk->reply_waiter = lkrq;
            cnt_replyq ++;
            send_act_update_to_slaves(lkrq);
            return 1;
         }else{
            send_act_lk_reply(lkrq, gio_Err_Ok);
         }
      }else
      {
         send_act_lk_reply(lkrq, err);
      }

   }/* while( !Qu_empty( CurrentQu ) ) */

   return 0;
}

/**
 * Run_state_Conflict_Queue - checks wait queue 
 * 
 * Returns: =0:Queue Emptied !0:Items still in Queue.
 */
int Run_state_Conflict_Queue(Qu_t *CurrentQu, Lock_t *lk)
{
   Qu_t *tmp;
   Waiters_t *lkrq;

   while( !Qu_empty( CurrentQu ) ) {
      /* need to take the req we're processing off the qu since the lock
       * state functions make decisions based on if there is reqs waiting
       * in the Qu.
       */
      tmp = Qu_DeQu( CurrentQu );
      lkrq = Qu_data(tmp);
      cnt_confq --;

      if( lkrq_onto_lock(lk, lkrq, FALSE) != 0 ) return 1;

   }/* while( !Qu_empty( CurrentQu ) ) */

   return 0;
}

/**
 * Run_state_Incomming_Queue - 
 * @CurrentQu: 
 * @lk: 
 * 
 * The only reason to leave an item on the incomming queue is because there
 * is an item in the reply_waiter.  Other wise the handling an item on the
 * head of the Incomming queue will ALWAYS pull it off.  Items are NEVER
 * requeued here.  They must either have an error sent back to the client,
 * send success and mod the lock, or push the item on the Conflict queue.
 * 
 * Returns: int
 */
int Run_state_Incomming_Queue(Qu_t *CurrentQu, Lock_t *lk)
{
   Qu_t *tmp;
   Waiters_t *lkrq;

   while( !Qu_empty( CurrentQu ) ) {
      tmp = Qu_DeQu( CurrentQu );
      lkrq = Qu_data(tmp);
      cnt_inq--;

      if( lkrq_onto_lock(lk, lkrq, TRUE) != 0 ) return 1;

   }/* while( !Qu_empty( CurrentQu ) ) */

   return 0;
}

/**
 * Run_WaitQu - 
 * @lk: 
 */
void Run_WaitQu(Lock_t *lk)
{
   /* when ever enough replies come back, this gets called again. */
   if( lk->reply_waiter != NULL ) return;

   if( Run_Action_Incomming_Queue( &lk->Action_Waiters, lk) != 0 ) return;
   if( Run_state_Incomming_Queue( &lk->State_Waiters, lk) != 0 ) return;

   if( Run_state_Conflict_Queue( &lk->High_Waiters, lk) != 0 ) return;
   if( Run_state_Conflict_Queue( &lk->Waiters, lk) != 0 ) return;
}

/**
 * check_lists_for_desyncs - 
 * @lk: 
 * 
 */
void check_lists_for_desyncs(Lock_t *lk)
{
   int i;
   /* check lists.
    * can probably remove these, the problem they were detecting is long
    * fixed.
    * */
   i = count_list( &lk->Holders );
   if( i != lk->HolderCount ) {
      die(ExitGulm_Assertion,
            "Holder list count desynced ( %d != %d ) on lock %s.\n",
            i, lk->HolderCount, lkeytob64(lk->key, lk->keylen));
   }
   i = count_list( &lk->ExpHolders );
   if( i != lk->ExpiredCount ) {
      die(ExitGulm_Assertion,
            "Exp list count desynced ( %d != %d ) on lock %s.\n",
            i, lk->ExpiredCount, lkeytob64(lk->key, lk->keylen));
   }
   i = count_list( &lk->LVB_holders );
   if( i != lk->LVB_holder_cnt ) {
      die(ExitGulm_Assertion,
            "LVB Holder list count desynced ( %d != %d ) on lock %s.\n",
            i, lk->LVB_holder_cnt, lkeytob64(lk->key, lk->keylen));
   }
   
}

/**
 * force_lock_state - 
 * @lkrq: 
 * 
 * forciably set this lock to the state.  Used in updates we get when in
 * slave mode.
 *
 * Returns: int
 */
int force_lock_state(Waiters_t *lkrq)
{
   Lock_t *lk;
   Holders_t *h;

   lk = find_lock( lkrq->key, lkrq->keylen );

   cur_lops++;

   check_lists_for_desyncs(lk);

   /* copy the LVB if we need to. */
   if(lkrq->state != gio_lck_st_Exclusive &&
      (lkrq->flags & gio_lck_fg_hasLVB) &&
      lk->HolderCount == 1 &&
      !LLi_empty(&lk->Holders) &&
      (h=LLi_data(LLi_next(&lk->Holders))) != NULL &&
      h->state == gio_lck_st_Exclusive &&
      lk->LVB != NULL && lkrq->LVB != NULL &&
      check_for_LVB_holder(lkrq->name, lk) 
     ) {
      lvbcpy(lk, lkrq);
   }

   if( lkrq->state == gio_lck_st_Unlock ) {
      drop_holder_by_range(lk, lkrq);
      check_for_recycle(lk);
   }else
   {
      if( lk->HolderCount != 0 )
         drop_holder_by_range(lk, lkrq);
      add_to_holders(lk, lkrq);
   }

   /* tell the master we got it. */
   send_update_reply_to_master(lkrq);

   /* all done. */
   recycle_lkrq(lkrq);

   return 0;
}

/**
 * do_lock_state - 
 * @lkrq: 
 * 
 * Handles Lock Transitions (changing the state of the lock)
 *
 * Returns: =0: Ok, <0: socketError
 */
int do_lock_state(Waiters_t *lkrq)
{
   int err = 0;
   Lock_t *lk;

   check_fullness();
   lk = find_lock( lkrq->key, lkrq->keylen );

   check_lists_for_desyncs(lk);

   cur_lops++;
   /* unless we're proven otherwise later, assume that this lock represents
    * cachable data.
    * This is purely an extra thing that gfs uses.
    */
   lkrq->flags |= gio_lck_fg_Cachable;

   /* check to see if they have an activity already. Only one action can be
    * pending per client per lock.
    * this should NEVER happen.  It violates how the IO works.
    * */
   if( check_for_waiter(lk, lkrq) ) {
      log_msg(lgm_locking,
            "Warning! Duplicate lock requests, using first one. %s %s\n",
            lkrq->name, lkeytob64(lk->key, lk->keylen));
      return gio_Err_AlreadyPend;/* wait for a reply first dolt.*/
   }

   /* don't think this ever happens, but.... */
   if( lkrq->state == gio_lck_st_Unlock &&
       lkrq->flags & gio_lck_fg_Try ) {
      lkrq->flags &= ~gio_lck_fg_Try; /* do or donot! There is no try. */
   }

   /* stick onto the State_Waiters and run queues. */
   if( lkrq->state == gio_lck_st_Unlock ||
       lkrq->flags & gio_lck_fg_Piority ) {
      /* do unlocks, and piorities first. */
      Qu_EnQu_Front(&lk->State_Waiters, &lkrq->wt_list);
   }else{
      Qu_EnQu(&lk->State_Waiters, &lkrq->wt_list);
   }
   cnt_inq ++;

   Run_WaitQu(lk);
   check_for_recycle(lk);
   return err;
}

/**
 * do_lock_query - 
 * @lkrq: 
 * 
 * 
 * Returns: int
 */
int do_lock_query(Waiters_t *lkrq)
{
   Lock_t *lk;
   LLi_t *tp;
   Holders_t *new, *h;

   lk = find_lock( lkrq->key, lkrq->keylen );
   cur_lops ++;

   /* check lock for conflicts. */
   if( !LLi_empty(&lk->Holders) ) {
      for(tp = LLi_next(&lk->Holders);
          NULL != LLi_data(tp);
          tp = LLi_next(tp)) {
         h = LLi_data(tp);
         if( Do_Holder_Waiter_conflict(h,lkrq) ) {
            new = duplicate_holder(h);
            LLi_add_after( &lkrq->holders, &new->cl_list );
            break;
         }
      }
   }

   return send_query_reply(lkrq, 0);
}

/**
 * force_lock_action - 
 * @lkrq: 
 * 
 * 
 * Returns: int
 */
int force_lock_action(Waiters_t *lkrq)
{
   Lock_t *lk;
   Holders_t *h;

   lk = find_lock( lkrq->key, lkrq->keylen);

   cur_lops ++;

   if( lkrq->state == gio_lck_st_Cancel ) {
      /* no cancel in force. */
      log_err("Slave got a cancel request!\n");
   }else
   if( lkrq->state == gio_lck_st_HoldLVB ) {
      if( ! check_for_LVB_holder(lkrq->name, lk) )
         add_to_LVB_holders(lkrq->name, lk);
   }else
   if( lkrq->state == gio_lck_st_UnHoldLVB ) {
      drop_LVB_holder(lkrq->name, lk);
      check_for_recycle(lk);
   }else
   if( lkrq->state == gio_lck_st_SyncLVB ) {
      if( lk->HolderCount == 1 &&
          !LLi_empty(&lk->Holders) &&
          (h=LLi_data(LLi_next(&lk->Holders))) != NULL &&
          h->state == gio_lck_st_Exclusive &&
          lk->LVB != NULL && lkrq->LVB != NULL &&
          check_for_LVB_holder(lkrq->name, lk) ) {
         lvbcpy(lk, lkrq);
      }
   }else
   {
      log_err("Unknown force action:%#x name:%s lock:%s \n", lkrq->state, 
              lkrq->name, lkeytob64(lkrq->key, lkrq->keylen));
   }

   send_update_reply_to_master(lkrq);
   recycle_lkrq(lkrq);
   return 0;
}

/**
 * do_lock_action - 
 * @lkrq: 
 * 
 * Handles lock Actions (stuff to do on locks)
 *
 * Returns: int
 */
int do_lock_action(Waiters_t *lkrq)
{
   int ret = 0;
   Lock_t *lk;

   check_fullness();
   lk = find_lock( lkrq->key, lkrq->keylen );

   cur_lops++;

   if( lkrq->state == gio_lck_st_Cancel ) {
      ret = cancel_waiting_lkrq(lk, lkrq);
      Run_WaitQu(lk);
      check_for_recycle(lk);
      return 0;
   }

   /* stick it on the queue */
   Qu_EnQu(&lk->Action_Waiters, &lkrq->wt_list);
   cnt_inq ++;
   Run_WaitQu(lk);
   check_for_recycle(lk);

   return 0;
}

/**
 * increment_slave_update_replies - 
 * @key: < key for the lock this affects
 * @len: < how long that key is
 * @slave: < which slave sent this reply
 * @smask: < bitmask of the connected slaves.
 *
 * 
 * Returns: int
 */
int increment_slave_update_replies(uint8_t *key, uint16_t len,
      int slave, uint8_t smask)
{
   Lock_t *lk;
   Waiters_t *lkrq;

   lk = find_lock(key, len);

   if( lk->reply_waiter == NULL ) {
      log_msg(lgm_Always, "There is no reply waiter on lock %s\n",
            lkeytob64(key,len));
      return -1;
   }
   lkrq = lk->reply_waiter;

   /* set bit */
   lkrq->Slave_rpls |= 1 << slave;

   /* check mask
    * there can be more bits set in the Slave_rpls than in smask
    * if every bit in smask is in Slave_rpls, then send client reply.
    *
    * First, if everyone we sent it to has replied, then we're good.
    * If not everyone we sent it to has replied; has everone that is alive
    * replied?  If so, we're good.
    *
    * mmmmShortCircuitLogic...
    * */
   if( (lkrq->Slave_rpls & lkrq->Slave_sent) == lkrq->Slave_sent ||
       (lkrq->Slave_rpls & smask) == smask ) {
      log_msg(lgm_LockUpdates, "Got all update replies for %s.\n",
            lkeytob64(lk->key, lk->keylen));
      /* all slaves have reported in. send reply to client. */
      lk->reply_waiter = NULL;
      cnt_replyq --;
      if(lkrq->op == gulm_lock_action_req ) {
         send_act_lk_reply(lkrq, gio_Err_Ok);
      }else
      if( lkrq->op == gulm_lock_state_req ) {
         send_req_lk_reply(lkrq, lk, gio_Err_Ok);
      }

      /* since that reply might have been blocking */
      Run_WaitQu(lk);
      check_for_recycle(lk);
   }

   return 0;
}

struct recheck_reply_waiters_s {
   uint8_t sms;
   uint8_t onlogin;
};
/**
 * _recheck_reply_waiters_ - 
 * @item: 
 * @d: 
 * 
 * 
 * Returns: int
 */
int _recheck_reply_waiters_(LLi_t *item, void *d)
{
   struct recheck_reply_waiters_s *rrw = (struct recheck_reply_waiters_s *)d;
   Lock_t *lk;
   Waiters_t *lkrq;

   lk = LLi_data(item);
   if( lk->reply_waiter != NULL ) {
      lkrq = lk->reply_waiter;

      if( rrw->onlogin != 0 ) {
         /* This slave just logged in, so they just grabbed the entire
          * lockspace.
          * So if there are any reply_waiters waiting on a reply from them,
          * they'll not get it, but it has effectively happened.
          * So we set reply bit.
          */
         if( (lkrq->Slave_sent & rrw->onlogin) == rrw->onlogin ) {
      log_msg(lgm_Always, "%s Sent but not recved on Slave login. Marking.\n",
            lkeytob64(lkrq->key, lkrq->keylen));
            lkrq->Slave_rpls |= rrw->onlogin;
         }
      }

      if( (lkrq->Slave_rpls & lkrq->Slave_sent) == lkrq->Slave_sent ||
          (lkrq->Slave_rpls & rrw->sms) == rrw->sms ) {
         log_msg(lgm_LockUpdates, "Recheck scan cleared reply for %s.\n",
               lkeytob64(lk->key, lk->keylen));
         /* all slaves have reported in. send reply to client. */
         lk->reply_waiter = NULL;
         cnt_replyq --;
         if(lkrq->op == gulm_lock_action_req ) {
            send_act_lk_reply(lkrq, gio_Err_Ok);
         }else
         if( lkrq->op == gulm_lock_state_req ) {
            send_req_lk_reply(lkrq, lk, gio_Err_Ok);
         }

         /* since that reply might have been blocking */
         Run_WaitQu(lk);
         check_for_recycle(lk);
      }

   }
   return 0;
}

/**
 * recheck_reply_waiters - 
 * 
 * Need something here to scan over the locks so that when a slave
 * dies/logs out we can send off any waiting locks replies that were
 * waiting for the slave.
 */
void recheck_reply_waiters(uint8_t Slave_bits, uint8_t onlogin)
{
   struct recheck_reply_waiters_s rrw;
   rrw.sms = Slave_bits;
   rrw.onlogin = onlogin;
   hash_walk(AllLocks, _recheck_reply_waiters_, &rrw);
}

/**
 * inner_expire_from_waiters - 
 * @list: 
 * @name: 
 * @lk: 
 * 
 * 
 * Returns: int
 */
int inner_expire_from_waiters(LLi_t *list, uint8_t *name, Lock_t *lk)
{
   LLi_t *tp, *next;
   Waiters_t *w;
   int found = FALSE;
   if( ! LLi_empty( list ) ) {
      for(tp=LLi_next(list);
          LLi_data(tp) != NULL;
          tp = next)
      {
         next = LLi_next(tp);
         w = LLi_data(tp);
         if( w->name != NULL && strcmp(w->name, name) == 0 ) {
            LLi_del(tp);
            recycle_lkrq(w);
            found = TRUE;
         }
      }
   }
   return found;
}

/**
 * expire_from_waiters - 
 * @name: 
 * @lk: 
 * 
 * 
 * Returns: int
 */
int expire_from_waiters(uint8_t *name, Lock_t *lk)
{
   Waiters_t *w;
   int A,B,C,D,E;

   A=B=C=D=E=FALSE;

   if( (w = lk->reply_waiter) != NULL &&
       w->name != NULL &&
       strcmp(w->name, name) == 0 ) {
      lk->reply_waiter = NULL;
      cnt_replyq --;
      recycle_lkrq(w);
      A = TRUE;
   }

   /* this is done the following because if we chained the function calls
    * together with || things won't behave the way we want it to.
    * (and if you don't know why that would be different, go back to
    * school.)
    */
   B = inner_expire_from_waiters(&lk->State_Waiters, name, lk);
   if( B ) cnt_inq --;
   C = inner_expire_from_waiters(&lk->Action_Waiters, name, lk);
   if( C ) cnt_inq --;
   D = inner_expire_from_waiters(&lk->High_Waiters, name, lk);
   if( D ) cnt_confq --;
   E = inner_expire_from_waiters(&lk->Waiters, name, lk);
   if( E ) cnt_confq --;

   return A || B || C || D || E;
}

/**
 * _expire_locks_ - The actual work to expire a lock
 * @item: 
 * @d: 
 * 
 * this function could probably stand to be rewritten.
 * 
 * Returns: int
 */
int _expire_locks_(LLi_t *item, void *d)
{
   uint8_t *name = (uint8_t*)d;
   Lock_t *lk;
   int modQ=FALSE;
   LLi_t *tp,*nxt;
   Holders_t *h;

   lk = LLi_data(item);

   /* drop from wait queue */
   if( expire_from_waiters(name, lk) ) modQ = TRUE;
   if( drop_LVB_holder(name, lk) == 0 ) {
      modQ = TRUE;
   }

   for(tp=LLi_next(&lk->Holders); LLi_data(tp) != NULL; tp=nxt) {
      nxt = LLi_next(tp);
      h = LLi_data(tp);
      if( strcmp(h->name, name) == 0 ) {
         LLi_del(tp);
         lk->HolderCount --;
         decrement_global_state_counters(h->state);
         switch(h->state) {
            case gio_lck_st_Exclusive:
               /* move to exp list */
               if( !check_for_expholder(h, lk ) ) {
                  move_to_Expholders(h, lk);
               }
               /* reset LVB */
               if( lk->LVB != NULL ) memset( lk->LVB, 0, lk->LVBlen);
               break;
            case gio_lck_st_Shared:
            case gio_lck_st_Deferred:
               /* just drop it... */
               modQ = TRUE;
               recycle_holder(h);
               break;
         }
      }
   }

   if( modQ ) {
      /* a change that might let queued requests advance */
      Run_WaitQu(lk);
      check_for_recycle(lk);
   }
   return 0;
}

/**
 * expire_locks - 
 * @Cname: < Name of Client who's locks we're expiring
 */
void __inline__ expire_locks(uint8_t *name)
{
   int e;
#ifdef TIME_RECOVERY_PARTS
   struct timeval tva, tvb;
   gettimeofday(&tva, NULL);
#endif

   e = hash_walk(AllLocks, _expire_locks_, name);
   if(e!=0) log_err("Got %d, trying to expire locks for %s\n",e,name);

#ifdef TIME_RECOVERY_PARTS
   gettimeofday(&tvb, NULL);
   log_bug("It took %ld sec and %ld micro sec to expire all locks.\n",
         (tvb.tv_sec - tva.tv_sec), (tvb.tv_usec - tva.tv_usec));
#endif
}

/**
 * cmp_lock_mask - 
 * @mask: 
 * @masklen: 
 * @key: 
 * @keylen: 
 * 
 * See if a key fits with the mask.
 *
 * ADD: if mask is sorter than key, ``extend'' mask with 0xff
 * 
 * Returns: TRUE || FALSE
 */
int cmp_lock_mask(uint8_t *mask, int masklen, uint8_t *key, int keylen)
{
   int i;
   for(i=0; i < masklen && i < keylen; i++ ) {
      /* mask byte == 0xff, matches everything
       * mask byte == rest, must == key byte.
       */
      if( mask[i] == 0xff ) continue;
      if( mask[i] != key[i] ) return FALSE;
   }
   /* key shorter than mask, doesn't match. */
   if( keylen < masklen ) return FALSE;
   /* key longer than mask, and prev bytes match, then all matches. */

   /* key fits within mask. */
   return TRUE;
}

typedef struct _drop_locks_s {
   uint8_t *name;
   uint8_t *mask;
   uint16_t mlen;
} _drop_locks_t;
/**
 * _drop_locks_ - The actual work to drop a lock
 * @item: 
 * @d: 
 * 
 * 
 * Returns: int
 */
int _drop_locks_(LLi_t *item, void *d)
{
   _drop_locks_t *dl = (_drop_locks_t*)d;
   Lock_t *lk;
   lk = LLi_data(item);

   if( dl->name != NULL ) {
      if( lk->ExpiredCount > 0 ) {
         if( cmp_lock_mask(dl->mask, dl->mlen, lk->key, lk->keylen) ) {
            drop_expholders(dl->name, lk); /* decrements counters for us */

            Run_WaitQu(lk);
            check_for_recycle(lk);
         }
      }
   }else{ /* doing a drop all exp */
      if( lk->ExpiredCount > 0 ) {
         if( cmp_lock_mask(dl->mask, dl->mlen, lk->key, lk->keylen) ) {
            delete_entire_holder_list( &lk->ExpHolders );
            cnt_exp_holds -= lk->ExpiredCount;
            lk->ExpiredCount = 0;
    
            Run_WaitQu(lk);
            check_for_recycle(lk);
         }
      }
   }

   return 0;
}


/**
 * drop_expired - 
 * @name: < Name of Client whose locks we're flushing.
 * @mask: < key mask.  Only drop exp on locks that match this mask.
 * @len: < length of mask
 *
 */
void __inline__ drop_expired(uint8_t *name, uint8_t *mask, uint16_t len)
{
   int e;
   _drop_locks_t dl;
#ifdef TIME_RECOVERY_PARTS
   struct timeval tva, tvb;
   gettimeofday(&tva, NULL);
#endif

   dl.name = name;
   dl.mask = mask;
   dl.mlen = len;

   e = hash_walk(AllLocks, _drop_locks_, &dl);
   if(e!=0) log_err("Got %d, trying to drop locks for %s\n",e,name);

#ifdef TIME_RECOVERY_PARTS
   gettimeofday(&tvb, NULL);
   log_bug("It took %ld sec and %ld micro sec to drop exp locks.\n",
         (tvb.tv_sec - tva.tv_sec), (tvb.tv_usec - tva.tv_usec));
#endif
}

/**
 * _rerun_wait_queues_ - 
 * @item: 
 * @d: 
 * 
 * 
 * Returns: int
 */
int _rerun_wait_queues_(LLi_t *item, void *d)
{
   Lock_t *lk;
   lk = LLi_data(item);
   Run_WaitQu(lk);
   check_for_recycle(lk);
   return 0;
}

/**
 * rerun_wait_queues - 
 */
void __inline__ rerun_wait_queues(void)
{
#ifdef TIME_RECOVERY_PARTS
   struct timeval tva, tvb;
   gettimeofday(&tva, NULL);
#endif
   hash_walk(AllLocks, _rerun_wait_queues_, NULL);

#ifdef TIME_RECOVERY_PARTS
   gettimeofday(&tvb, NULL);
   log_bug("It took %ld sec and %ld micro sec to rerun wait queues.\n",
         (tvb.tv_sec - tva.tv_sec), (tvb.tv_usec - tva.tv_usec));
#endif
}

/*****************************************************************************/
/**
 * _serialize_lockspace_ - 
 * @item: 
 * @d: 
 * 
 * 
 * Returns: int
 */
int _serialize_lockspace_(LLi_t *item, void *d)
{
   xdr_enc_t *xdr = (xdr_enc_t*)d;
   Lock_t *lk;
   LLi_t *tp;
   Holders_t *h;
   int err;

   lk = LLi_data(item);

   if((err = xdr_enc_uint8(xdr, lk->keylen)) != 0 ) return err;
   if((err = xdr_enc_raw(xdr, lk->key, lk->keylen)) != 0 ) return err;
   if((err = xdr_enc_uint8(xdr, lk->LVBlen)) != 0 ) return err;
   if( lk->LVBlen > 0 ) {
      if((err = xdr_enc_raw(xdr, lk->LVB, lk->LVBlen)) != 0 ) return err;
   }

   if((err = xdr_enc_uint32(xdr, lk->HolderCount)) != 0 ) return err;
   if((err = xdr_enc_list_start(xdr)) != 0 ) return err;
   for(tp=LLi_next(&lk->Holders); LLi_data(tp) != NULL; tp=LLi_next(tp)) {
      h = LLi_data(tp);
      if((err = xdr_enc_string(xdr, h->name)) != 0 ) return err;
      if((err = xdr_enc_uint64(xdr, h->subid)) != 0 ) return err;
      if((err = xdr_enc_uint8(xdr, h->state)) != 0 ) return err;
      if((err = xdr_enc_uint64(xdr, h->start)) != 0 ) return err;
      if((err = xdr_enc_uint64(xdr, h->stop)) != 0 ) return err;
   }
   if((err = xdr_enc_list_stop(xdr)) != 0 ) return err;

   if((err = xdr_enc_uint32(xdr, lk->LVB_holder_cnt)) != 0 ) return err;
   if((err = xdr_enc_list_start(xdr)) != 0 ) return err;
   for(tp=LLi_next(&lk->LVB_holders); LLi_data(tp) != NULL; tp=LLi_next(tp)) {
      h = LLi_data(tp);
      if((err = xdr_enc_string(xdr, h->name)) != 0 ) return err;
      if((err = xdr_enc_uint64(xdr, h->subid)) != 0 ) return err;
   }
   if((err = xdr_enc_list_stop(xdr)) != 0 ) return err;

   if((err = xdr_enc_uint32(xdr, lk->ExpiredCount)) != 0 ) return err;
   if((err = xdr_enc_list_start(xdr)) != 0 ) return err;
   for(tp=LLi_next(&lk->ExpHolders); LLi_data(tp) != NULL; tp=LLi_next(tp)) {
      h = LLi_data(tp);
      if((err = xdr_enc_string(xdr, h->name)) != 0 ) return err;
      if((err = xdr_enc_uint64(xdr, h->subid)) != 0 ) return err;
   }
   if((err = xdr_enc_list_stop(xdr)) != 0 ) return err;

   /* we don't track the waiter queues. so we're done with this lock. */
   return 0;
}

/**
 * serialize_lockspace - 
 * @fd: 
 * 
 *
 * Returns: int
 */
int serialize_lockspace(int fd)
{
   xdr_enc_t *xdr;
   int err;

   xdr = xdr_enc_init(fd, (1024*1024) ); /* 1M buffer */
   if( xdr == NULL ) return -ENOMEM;

   xdr_enc_list_start(xdr);

   if(hash_walk(AllLocks, _serialize_lockspace_, xdr) != 0 ) {
      xdr_enc_force_release(xdr);
      return -1;
   }

   xdr_enc_list_stop(xdr);

   err = xdr_enc_flush(xdr);
   xdr_enc_force_release(xdr);
   return err;
}


/**
 * deserialize_lockspace - 
 * @fd: 
 * 
 * 
 * Returns: int
 */
int deserialize_lockspace(int fd)
{
   xdr_dec_t *xdr;
   uint8_t key[1024], *name, keylen;
   uint16_t len;
   int counter,err;
   Lock_t *lk;
   Holders_t *h;
   
   clear_lockspace();
   
   xdr = xdr_dec_init(fd, 0); /* just use defaults here. */
   if( xdr == NULL ) return -ENOMEM;

   if( (err=xdr_dec_list_start(xdr)) != 0 ) goto fail;

   while( xdr_dec_list_stop(xdr) != 0 ) {
      if( (err=xdr_dec_uint8(xdr, &keylen)) != 0 ) goto fail;
      len = keylen;
      if( (err=xdr_dec_raw(xdr, key, &len)) != 0 ) goto fail;
      lk = find_lock(key, keylen);

      if( (err=xdr_dec_uint8(xdr, &lk->LVBlen)) != 0 ) goto fail;
      if( lk->LVBlen > 0 ) {
         lk->LVB = malloc(lk->LVBlen);
         if( lk->LVB == NULL ) {err=-ENOMEM;goto fail;}
         len = lk->LVBlen;
         if( (err=xdr_dec_raw(xdr, lk->LVB, &len)) != 0 ) goto fail;
      }else{
         lk->LVB = NULL;
      }

      if( (err=xdr_dec_uint32(xdr, &lk->HolderCount)) != 0 ) goto fail;
      counter = 0;
      if( (err=xdr_dec_list_start(xdr)) != 0 ) goto fail;
      while( xdr_dec_list_stop(xdr) != 0 ) {
         if( (err=xdr_dec_string(xdr, &name)) != 0 ) goto fail;
         h = get_new_holder();
         if( h == NULL ) {err=-ENOMEM;goto fail;}
         LLi_init(&h->cl_list, h);
         h->name = name;
         h->idx = 0;
         if( (err=xdr_dec_uint64(xdr, &h->subid)) != 0 ) goto fail;
         if( (err=xdr_dec_uint8(xdr, &h->state)) != 0 ) goto fail;
         increment_global_state_counters(h->state);
         if( (err=xdr_dec_uint64(xdr, &h->start)) != 0 ) goto fail;
         if( (err=xdr_dec_uint64(xdr, &h->stop)) != 0 ) goto fail;
         LLi_add_after( &lk->Holders, &h->cl_list);
         counter ++;
      }
      if( counter != lk->HolderCount ) {
         log_msg(lgm_Always,"AH! counter != lk->HolderCount %d != %d "
                 "Using counter.\n", counter, lk->HolderCount);
         lk->HolderCount = counter;
      }

      if( (err=xdr_dec_uint32(xdr, &lk->LVB_holder_cnt)) != 0 ) goto fail;
      counter = 0;
      if( (err=xdr_dec_list_start(xdr)) != 0 ) goto fail;
      while( xdr_dec_list_stop(xdr) != 0 ) {
         if( (err=xdr_dec_string(xdr, &name)) != 0 ) goto fail;
         h = get_new_holder();
         if( h == NULL ) {err=-ENOMEM;goto fail;}
         LLi_init(&h->cl_list, h);
         h->name = name;
         h->idx = 0;
         if( (err=xdr_dec_uint64(xdr, &h->subid)) != 0 ) goto fail;
         LLi_add_after( &lk->LVB_holders, &h->cl_list);
         counter ++;
         cnt_lvb_holds ++;
      }
      if( counter != lk->LVB_holder_cnt ) {
         log_msg(lgm_Always,"AH! counter != lk->LVB_holder_cnt %d != %d "
                 "Using counter.\n", counter, lk->LVB_holder_cnt);
         lk->LVB_holder_cnt = counter;
      }

      if( (err=xdr_dec_uint32(xdr, &lk->ExpiredCount)) != 0 ) goto fail;
      counter = 0;
      if( (err=xdr_dec_list_start(xdr)) != 0 ) goto fail;
      while( xdr_dec_list_stop(xdr) != 0 ) {
         if( (err=xdr_dec_string(xdr, &name)) != 0 ) goto fail;
         h = get_new_holder();
         if( h == NULL ) {err=-ENOMEM;goto fail;}
         LLi_init(&h->cl_list, h);
         h->name = name;
         h->idx = 0;
         if( (err=xdr_dec_uint64(xdr, &h->subid)) != 0 ) goto fail;
         LLi_add_after( &lk->ExpHolders, &h->cl_list);
         counter ++;
         cnt_exp_holds ++;

      }
      if( counter != lk->ExpiredCount ) {
         log_msg(lgm_Always,"AH! counter != lk->ExpiredCount %d != %d "
                 "Using counter.\n", counter, lk->ExpiredCount);
         lk->ExpiredCount = counter;
      }
   }

   xdr_dec_release(xdr);
   return 0;
fail:
   xdr_dec_release(xdr);
   return err;
}

/* vim: set ai cin et sw=3 ts=3 : */
