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

/* private details that we don't want to give the users of this lib access
 * to go here.
 */

#ifdef __KERNEL__

#ifdef __linux__
#include <linux/kernel.h>
#include <linux/sched.h>
#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>
#endif /*__linux__*/


#define lg_malloc(y) kmalloc(y, GFP_KERNEL)
#define lg_free(x,y) kfree(x)
#define lg_str_free(x) kfree( (x) , strlen( x ) + 1 )
#define lg_memcpy(x,y,l) memcpy(x,y,l)

struct lg_mutex
{
  struct semaphore sema;
};
typedef struct lg_mutex lg_mutex_t;

#define lg_mutex_init(x) do { init_MUTEX(&(x)->sema); } while (0)
#define lg_mutex_destroy(x)

#define lg_mutex_lock(x) do { down(&(x)->sema); } while (0)
#define lg_mutex_trylock(x) (!down_trylock(&(x)->sema))
#define lg_mutex_unlock(x) do { up(&(x)->sema); } while (0)

#else /*__KERNEL__*/
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdint.h>

#define lg_malloc(y) malloc(y)
#define lg_free(x,y) free(x)
#define lg_str_free(x) free(x)
#define lg_memcpy(x,y,l) memcpy(x,y,l)

struct lg_mutex
{
   pthread_mutex_t lk;
};
typedef struct lg_mutex lg_mutex_t;

#define lg_mutex_init(x) do { pthread_mutex_init(&(x)->lk,NULL); } while(0)
#define lg_mutex_destroy(x)  do { pthread_mutex_destroy(&(x)->lk); } while(0)

#define lg_mutex_lock(x) do { pthread_mutex_lock(&(x)->lk); } while(0)
#define lg_mutex_trylock(x) do { pthread_mutex_trylock(&(x)->lk); } while(0)
#define lg_mutex_unlock(x) do { pthread_mutex_unlock(&(x)->lk); } while(0)

#endif /*__KERNEL__*/

#include "xdr.h"
#include "gio_wiretypes.h"
#include "libgulm.h"

#if !defined(TRUE) || !defined(FALSE)
#undef TRUE
#undef FALSE
#define TRUE  (1)
#define FALSE (0)
#endif

#define LGMAGIC (0x474d4354)

struct gulm_interface_s {
   /* since we've masked this to a void* to the users, it is a nice safty
    * net to put a little magic in here so we know things stay good.
    */
   uint32_t first_magic;

   /* WHAT IS YOUR NAME?!? */
   char *service_name;

   char *clusterID;

   uint16_t core_port;
   xdr_socket core_fd;
   xdr_enc_t *core_enc;
   xdr_dec_t *core_dec;
   lg_mutex_t core_sender;
   lg_mutex_t core_recver;
   int in_core_hm;

   uint16_t lock_port;
   xdr_socket lock_fd;
   xdr_enc_t *lock_enc;
   xdr_dec_t *lock_dec;
   lg_mutex_t lock_sender;
   lg_mutex_t lock_recver;
   int in_lock_hm;
   uint8_t lockspace[4];

   /* in the message recver func, we read data into these buffers and pass
    * them to the callback function.  This way we avoid doing mallocs and
    * frees on every callback.
    */
   uint16_t cfba_len;
   uint8_t *cfba;
   uint16_t cfbb_len;
   uint8_t *cfbb;
   uint16_t lfba_len;
   uint8_t *lfba;
   uint16_t lfbb_len;
   uint8_t *lfbb;

   uint32_t last_magic;
};
typedef struct gulm_interface_s gulm_interface_t;

/* vim: set ai cin et sw=3 ts=3 : */
