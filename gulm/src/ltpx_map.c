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
#include <sys/time.h>

#include "gulm_defines.h"
#include "hash.h"
#include "LLi.h"
#include "ltpx_priv.h"
#include "config_gulm.h"
#include "utils_dir.h"

/*****************************************************************************/
/* bits of data used by the log_*() and die() functions. */
extern uint32_t verbosity;
extern char *ProgramName;
extern gulm_config_t gulm_config;

unsigned long free_reqs = 0;
unsigned long used_reqs = 0;

/*****************************************************************************/
unsigned char *getlkxpname(void *item) {
   lock_req_t *c = (lock_req_t*)item;
   return c->key;
}
int getlkxpnlen(void *item) {
   lock_req_t *c = (lock_req_t*)item;
   return c->keylen;
}

/*****************************************************************************/
/* Free pool */
LLi_t Free_lock_reqs;

/**
 * prealloc_reqs - 
 * @oid: 
 * 
 * 
 * Returns: int
 */
int prealloc_reqs(void)
{
   int i;
   lock_req_t *lq;
   for(i = 0; i < 90000; i++) {
      lq = malloc(sizeof(lock_req_t));
      memset(lq, 0, sizeof(lock_req_t));
      LLi_init(&lq->ls_list, lq);
      LLi_add_after(&Free_lock_reqs, &lq->ls_list);
      free_reqs ++;
   }
   return 0;
}

int initialize_ltpx_maps(void)
{
   LLi_init_head( &Free_lock_reqs );
   prealloc_reqs();
   return 0;
}

/**
 * get_new_lock_req - 
 * @oid: 
 * 
 * 
 * Returns: lock_req_t
 */
lock_req_t *get_new_lock_req(void)
{
   lock_req_t *lq;

   if( !LLi_empty(&Free_lock_reqs) ) {
      LLi_t *tmp;
      tmp = LLi_pop(&Free_lock_reqs);
      lq = LLi_data(tmp);
      free_reqs --;
   }else{
      lq = malloc(sizeof(lock_req_t));
      if( lq == NULL ) die(ExitGulm_NoMemory, "Out of memory.\n");
      memset(lq, 0, sizeof(lock_req_t));
   }
   used_reqs ++;
   LLi_init( &lq->ls_list, lq);
   lq->code = 0;
   lq->key = NULL;
   lq->keylen = 0;
   lq->state = 0;
   lq->flags = 0;
   lq->lvb = NULL;
   lq->lvblen = 0;
   lq->poll_idx = -1;

   return lq;
}

/**
 * recycle_lock_req - 
 * @lq: 
 * 
 * 
 * Returns: void
 */
void recycle_lock_req(lock_req_t *lq)
{

   if( lq->key != NULL ) {
      free(lq->key);
      lq->key = NULL;
   }
   if( lq->lvb != NULL ) {
      free(lq->lvb);
      lq->lvb = NULL;
   }
   lq->code = 0;
   lq->keylen = 0;
   lq->state = 0;
   lq->flags = 0;
   lq->lvblen = 0;
   lq->poll_idx = -1;

   LLi_unhook(&lq->ls_list);

   LLi_add_after(&Free_lock_reqs, &lq->ls_list);
   used_reqs --;
   free_reqs ++;

   /* get this into config too. */
   if( free_reqs > 90000 ) {
      LLi_t *tmp;
      tmp = LLi_prev(&Free_lock_reqs);
      LLi_del(tmp);
      lq = LLi_data(tmp);
      free(lq);
      free_reqs --;
   }
}

/**
 * create_new_req_map - 
 * 
 * Returns: hash_t
 */
hash_t *create_new_req_map(void)
{
   /* ??other thigns to init?? */
   return hash_create(gulm_config.lt_hashbuckets, getlkxpname, getlkxpnlen);
}

/**
 * release_req_map - 
 * @map: 
 *
 * mostly just here for completeness. duno if i'll use it.
 * 
 */
void release_req_map(hash_t *map)
{
   hash_destroy(map);
}

/* add item */

/* find/remove item */

/* walk over all items */

char *lkeytohex(uint8_t *key, uint8_t keylen);
char *lvbtohex(uint8_t *lvb, uint8_t lvblen);

static void print_lock_req(FILE *FP, lock_req_t *lq)
{
   fprintf(FP, "%s : \n", lkeytohex(lq->key, lq->keylen));
   fprintf(FP, " code : %#x\n", lq->code);
   fprintf(FP, " state : %#x\n", lq->state);
   fprintf(FP, " flags : %#x\n", lq->flags);
   fprintf(FP, " lvb : %s\n", lvbtohex(lq->lvb, lq->lvblen));
   fprintf(FP, " poll_idx : %d\n", lq->poll_idx);
}

static int _dump_lqs_(LLi_t *item, void *d)
{
   lock_req_t *lq;
   FILE* fp;

   lq = LLi_data(item);
   fp = (FILE*)d;
   fprintf(fp, "#======================================="
               "========================================\n");
   print_lock_req(fp, lq);
   return 0;
}

void dump_ltpx_locks(hash_t *map, int ltid)
{
   char *path;
   FILE *fp;

   if( build_tmp_path("Gulm_LTPX_Req_Dump", &path ) != 0 ) return;
   
   if( (fp = fopen(path,"a")) == NULL) {free(path); return;}

   fprintf(fp, "---\n# BEGIN LTPX REQ HASH DUMP FOR %d\n", ltid);

   hash_walk(map, _dump_lqs_, fp);

   fprintf(fp, "#======================================="
               "========================================\n");

   fprintf(fp, "# END LTPX REQ HASH DUMP FOR %d\n", ltid);

   fprintf(fp, "#======================================="
               "========================================\n");
   fclose(fp);
   free(path);
}

void dump_ltpx_senders(Qu_t *head, int ltid)
{
   char *path;
   FILE *fp;
   lock_req_t *lq;
   Qu_t *tmp;

   if( build_tmp_path("Gulm_LTPX_Req_Dump", &path ) != 0 ) return;
   
   if( (fp = fopen(path,"a")) == NULL) {free(path); return;}

   fprintf(fp, "---\n# BEGIN LTPX REQ SENDERS DUMP FOR %d\n", ltid);

   for(tmp = LLi_next(head);
       NULL != LLi_data(tmp);
       tmp = LLi_next(tmp))
   {
      lq = LLi_data(tmp);
      print_lock_req(fp, lq);
   }

   fprintf(fp, "#======================================="
               "========================================\n");

   fprintf(fp, "# END LTPX REQ SENDERS DUMP FOR %d\n", ltid);

   fprintf(fp, "#======================================="
               "========================================\n");
   fclose(fp);
   free(path);
}

