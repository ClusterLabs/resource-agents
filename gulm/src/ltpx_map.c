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
#include "hashn.h"
#include "LLi.h"
#include "ltpx_priv.h"
#include "config_gulm.h"
#include "utils_dir.h"
#include "utils_crc.h"
#include "utils_tostr.h"

/*****************************************************************************/
/* bits of data used by the log_*() and die() functions. */
extern uint32_t verbosity;
extern char *ProgramName;
extern gulm_config_t gulm_config;

unsigned long free_reqs = 0;
unsigned long used_reqs = 0;

/*****************************************************************************/
/**
 * lq_cmp - 
 * @a: 
 * @b: 
 * 
 * -1 if a < b
 *  0 if a == b
 *  1 if a > b
 * Returns: int
 */
int lq_cmp(void *a, void *b)
{
   lock_req_t *lqA = (lock_req_t *)a;
   lock_req_t *lqB = (lock_req_t *)b;

   if( lqA->subid == lqB->subid ) {
      return memcmp(lqA->key, lqB->key, MIN(lqA->keylen, lqB->keylen));
   }else
   if( lqA->subid < lqB->subid ) {
      return -1;
   }else
   {
      return 1;
   }
}
/**
 * lq_hash - 
 * @a: 
 * 
 * 
 * Returns: int
 */
int lq_hash(void *a)
{
   lock_req_t *lqA = (lock_req_t *)a;
   int ck;
   ck = crc32(lqA->key, lqA->keylen, lqA->keylen);
   ck = crc32((uint8_t*)&lqA->subid, sizeof(uint64_t), ck);
   return ck;
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
   lq->subid = 0;
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
hashn_t *create_new_req_map(void)
{
   /* ??other thigns to init?? */
   return hashn_create(gulm_config.lt_hashbuckets, lq_cmp, lq_hash);
}

/**
 * release_req_map - 
 * @map: 
 *
 * mostly just here for completeness. duno if i'll use it.
 * 
 */
void release_req_map(hashn_t *map)
{
   hashn_destroy(map);
}

/* add item */

/* find/remove item */

/* walk over all items */

char *lkeytohex(uint8_t *key, uint8_t keylen);
char *lvbtohex(uint8_t *lvb, uint8_t lvblen);

static void print_lock_req(FILE *FP, lock_req_t *lq)
{
   fprintf(FP, "%s : \n", lkeytohex(lq->key, lq->keylen));
   fprintf(FP, " subid : %"PRIu64"\n", lq->subid);
   fprintf(FP, " code : %s\n", gio_opcodes(lq->code));
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

void dump_ltpx_stuff(Qu_t *head, hashn_t *map, int ltid)
{
   int fd;
   FILE *fp;
   LLi_t *tmp;
   lock_req_t *lq;

   if( (fd=open_tmp_file("Gulm_LTPX_Req_Dump")) < 0 ) return;
   
   if( (fp = fdopen(fd,"a")) == NULL) return;

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
   fprintf(fp, "---\n# BEGIN LTPX REQ HASH DUMP FOR %d\n", ltid);

   if( map != NULL ) hashn_walk(map, _dump_lqs_, fp);

   fprintf(fp, "#======================================="
               "========================================\n");
   fprintf(fp, "# END LTPX REQ HASH DUMP FOR %d\n", ltid);
   fprintf(fp, "#======================================="
               "========================================\n");
   fclose(fp);
}

/* vim: set ai cin et sw=3 ts=3 : */
