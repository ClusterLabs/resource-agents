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
#include "myio.h"
#include "LLi.h"
#include "Qu.h"
#include "hash.h"
#include "gio_wiretypes.h"
#include "xdr.h"
#include "config_gulm.h"
#include "lock_priv.h"
#include "nodel.h"
#include "utils_ip.h"
#include "utils_tostr.h"
#include "utils_verb_flags.h"


/*****************************************************************************/
/* bits of data used by the log_*() and die() functions. */
extern uint32_t verbosity;
extern char *ProgramName;

extern struct in6_addr myIP;
extern char myName[];

/* confed things. */
extern gulm_config_t gulm_config;
extern char *LTname;
extern int LTid;

static int running = TRUE;
static struct timeval Started_at;
static struct timeval NOW;
static ip_name_t MasterIN = {{NULL,NULL,NULL},IN6ADDR_ANY_INIT,NULL};
static int I_am_the = gio_Mbr_ama_Pending;
static hash_t *nodelists=NULL;
/* this is true after we send a login request to the master, and are
 * waiting for the login reply from the master.
 *  it is so that we don't send multiple login requests without receiving a
 * login reply first.
 */
static int logging_into_master = FALSE;
static int PartialLockspace = FALSE;

/*****************************************************************************/
#ifdef DEBUG_LVB
#define lvb_log_msg(fmt, args...) log_msg(lgm_Always , fmt , ## args )
#else /*DEBUG_LVB*/
#define lvb_log_msg(fmt, args...)
#endif /*DEBUG_LVB*/
char __inline__ *lvbtohex(uint8_t *lvb, uint8_t lvblen);
/*****************************************************************************/


/*****************************************************************************/
/* Track which slaves are hooked to us
 *
 * There CANNOT be more than four slaves.  Ever.  There are bitmasks that
 * map to this list, and they are only one byte big.
 *
 * I am so silly sometimes.  a byte has eight bits, not four.
 *
 * Also, note that once entered, and name is never removed.  Should not be
 * an issue, since the servers list in the config cannot change either.
 *
 * but someday we do want to have that list change, which means this will
 * need to as well.  must think on this.
 */
typedef struct {
   int live; /* are they logged into us right now? */
   int idx; /* the poller index */
   char *name;
}slst_t;
static slst_t Slaves[4];
static int Slave_count = 0;
static uint8_t Slave_bitmask = 0;

/* Drop requests to nodes that don't currently have a socket need to be
 * saved until they reconenct.
 */
static LLi_Static_head_init(DropRequestPlaybackQueue);
unsigned long cnt_drpq = 0;

/**
 * init_lt_slave_list - 
 */
void init_lt_slave_list(void)
{
   int i;
   for(i=0;i<4;i++) {
      Slaves[i].live = FALSE;
      Slaves[i].name = NULL;
      Slaves[i].idx = -1;
   }
}

/**
 * add_to_slavelist - 
 * @fd: 
 * 
 * 
 * Returns: int
 */
static int add_to_slavelist(int idx, char *name)
{
   int i, empty=-1, found=FALSE;

   if( Slave_count == 4 ) return -EINVAL;

   for(i=0;i<4;i++) {
      if( Slaves[i].name == NULL ) {
         empty = i;
      }else
      if( strcmp(Slaves[i].name, name ) == 0 ) {
         Slaves[i].live = TRUE;
         Slaves[i].idx = idx;
         Slave_bitmask |= 1 << (i & 0x7 );
         found = TRUE;
         log_msg(lgm_Network2, "Added Slave %s to list at %d.\n", 
               Slaves[i].name, i);
         Slave_count ++;
         return 0;
      }
   }
   if( ! found ) {
      if( empty == -1 ) return -EINVAL;
      Slaves[empty].name = strdup(name);
      if( Slaves[empty].name == NULL ) return -ENOMEM;
      Slaves[empty].live = TRUE;
      Slaves[empty].idx = idx;
      Slave_bitmask |= 1 << (empty & 0x7 );
      log_msg(lgm_Network2, "Added Slave %s to list at %d.\n", 
            Slaves[empty].name, empty);
      Slave_count ++;
   }
   return 0;
}

/**
 * remove_slave_from_list - 
 * @idx: 
 * 
 */
static void remove_slave_from_list(int idx)
{
   int i;

   for(i=0; i < 4; i++ ) {
      if( Slaves[i].idx == idx ) {
         log_msg(lgm_Network2, "Removed Slave %s from %d in list.\n",
               Slaves[i].name, i);
         Slaves[i].live = FALSE;
         Slaves[i].idx = -1;
         Slave_bitmask &= ~( 1 << (i & 0x7) );
         Slave_count --;
      }
   }
}

/**
 * remove_slave_from_list_by_name - 
 * @name: 
 */
static void remove_slave_from_list_by_name(char *name)
{
   int i;
   for(i =0; i < 4; i++ ) {
      if( Slaves[i].name != NULL &&
          strcmp(Slaves[i].name, name) == 0 ) {
         log_msg(lgm_Network2, "Removed Slave %s from %d in list.\n",
               Slaves[i].name, i);
         Slaves[i].live = FALSE;
         Slaves[i].idx = -1;
         Slave_bitmask &= ~( 1 << (i & 0x7) );
         Slave_count --;
      }
   }
}

/**
 * get_slave_idx - 
 * @name: 
 * 
 * 
 * Returns: int
 */
static int get_slave_idx(char *name)
{
   int i;
   for(i=0;i<4;i++) {
      if( Slaves[i].name != NULL &&
          strcmp(Slaves[i].name, name) == 0 ) {
         return Slaves[i].idx;
      }
   }
   return -1;
}

static int get_slave_offset(int idx)
{
   int i;
   for(i=0;i<4;i++) {
      if( Slaves[i].idx == idx ) {
         return i;
      }
   }
   return -1;
}

/**
 * dump_slave_list - 
 * @enc: 
 * 
 * 
 * Returns: int
 */
static int dump_slave_list(xdr_enc_t *enc)
{
   int err, i;
   if((err=xdr_enc_list_start(enc))!=0) return err;
   for(i=0;i<4;i++) {
      if( Slaves[i].name != NULL && Slaves[i].live ) {
         if((err=xdr_enc_string(enc, Slaves[i].name))!=0) return err;
         if((err=xdr_enc_uint32(enc, Slaves[i].idx))!=0) return err;
      }
   }
   if((err=xdr_enc_list_stop(enc))!=0) return err;
   return xdr_enc_flush(enc);
}

/*****************************************************************************/

typedef enum {poll_Closed, poll_Accepting, poll_Trying, poll_Open} poll_state;
typedef enum {poll_Nothing, poll_Internal, poll_Slave, poll_Client} poll_type;
struct {
   struct pollfd *polls;
   xdr_enc_t    **enc;
   xdr_dec_t    **dec;
   poll_state    *state;
   poll_type     *type;
   uint64_t      *times;
   ip_name_t     *ipn;

   Qu_t          *outq;
   uint32_t      *outqlen;

   unsigned int maxi;
   int coreIDX;  /* link to core updates. */
   int listenFD; /* socket for new connections. */
   int MasterIDX; /* If we're a slave, where the Master is. */
} poller;
unsigned long totaloutqlen = 0;

int init_lt_poller(void)
{
   int i;

   memset(&poller, 0, sizeof(poller));

   poller.polls = malloc(open_max() * sizeof(struct pollfd));
   if( poller.polls == NULL ) goto nomem;
   memset(poller.polls, 0, (open_max() * sizeof(struct pollfd)));

   poller.state = malloc(open_max() * sizeof(poll_state));
   if( poller.state == NULL ) goto nomem;

   poller.type = malloc(open_max() * sizeof(poll_type));
   if( poller.type == NULL ) goto nomem;

   poller.times = malloc(open_max() * sizeof(uint64_t));
   if( poller.times == NULL ) goto nomem;

   poller.ipn = malloc(open_max() * sizeof(ip_name_t));
   if( poller.ipn == NULL ) goto nomem;

   poller.enc = malloc(open_max() * sizeof(xdr_enc_t*));
   if( poller.enc == NULL ) goto nomem;

   poller.dec = malloc(open_max() * sizeof(xdr_dec_t*));
   if( poller.dec == NULL ) goto nomem;

   poller.outq = malloc(open_max() * sizeof(Qu_t));
   if( poller.outq == NULL ) goto nomem;

   poller.outqlen = malloc(open_max() * sizeof(uint32_t));
   if( poller.outqlen == NULL ) goto nomem;

   for(i=0; i < open_max(); i++) {
      poller.polls[i].fd = -1;
      poller.state[i] = poll_Closed;
      poller.type[i] = poll_Nothing;
      poller.times[i] = 0;
      memset(&poller.ipn[i].ip, 0, sizeof(struct in6_addr));
      poller.ipn[i].name = NULL;
      poller.enc[i] = NULL;
      poller.dec[i] = NULL;
      Qu_init_head( &poller.outq[i] );
      poller.outqlen[i] = 0;
   }

   poller.maxi = 0;
   poller.coreIDX = -1;
   poller.listenFD = -1;
   poller.MasterIDX = -1;

   return 0;
nomem:
   if(poller.polls) free(poller.polls);
   if(poller.state) free(poller.state);
   if(poller.type) free(poller.type);
   if(poller.times) free(poller.times);
   if(poller.ipn) free(poller.ipn);
   if(poller.enc) free(poller.enc);
   if(poller.dec) free(poller.dec);
   if(poller.outq) free(poller.outq);
   if(poller.outqlen) free(poller.outqlen);
   return -ENOMEM;
}

void release_lt_poller(void)
{
   if(poller.polls) free(poller.polls);
   if(poller.state) free(poller.state);
   if(poller.type) free(poller.type);
   if(poller.times) free(poller.times);
   if(poller.ipn) free(poller.ipn);
   if(poller.enc) free(poller.enc);
   if(poller.dec) free(poller.dec);
   if(poller.outq) free(poller.outq);
   if(poller.outqlen) free(poller.outqlen);
}

static int add_to_pollers(int fd, poll_state p, uint64_t t,
                          const char *name, const struct in6_addr *ip)
{
   int i;
   for(i=0; poller.polls[i].fd >=0 && i< open_max(); i++);
   if( i>= open_max() ) return -1;

   if(fcntl(fd, F_SETFD, FD_CLOEXEC ) <0) return -1; /* close on exec. */

   poller.polls[i].fd = fd;
   poller.polls[i].events = POLLIN;
   if(i> poller.maxi) poller.maxi = i;
   poller.state[i] = p;
   poller.times[i] = t;
   memcpy(&poller.ipn[i].ip, ip, sizeof(struct in6_addr));
   if( name != NULL ) poller.ipn[i].name = strdup(name);
   else poller.ipn[i].name = NULL;
   poller.enc[i] = NULL;
   poller.dec[i] = NULL;
   /* you need to do the xdr seperate. */

   return i;
}

static int add_xdr_to_poller(int idx)
{
   if( idx < 0 ) return idx;
   poller.enc[idx] = xdr_enc_init( poller.polls[idx].fd, 396);
   if( poller.enc[idx] == NULL ) return -ENOMEM;
   poller.dec[idx] = xdr_dec_init( poller.polls[idx].fd, 396);
   if( poller.dec[idx] == NULL ) {
      xdr_enc_release(poller.enc[idx]);
      poller.enc[idx] = NULL;
      return -ENOMEM;
   }
   return 0;
}

/**
 * close_by_idx - 
 * @idx: 
 * 
 * 
 */
static void __inline__ close_by_idx(int idx)
{
   if( idx < 0 || idx > open_max() ) return;
   log_msg(lgm_Network2, "Closing connection idx:%d, fd:%d to %s\n",
         idx, poller.polls[idx].fd, poller.ipn[idx].name);
   /* If we just closed the connect to the Master, set things up to try to
    * re-find it.
    * gotta do this before I wipe all the info out.
    */
   if( poller.MasterIDX != -1 && idx == poller.MasterIDX ) {
      poller.MasterIDX = -1;
      log_msg(lgm_Network2, "Connection to Master closed.\n");
   }

   if( poller.coreIDX == idx ) 
      die(ExitGulm_Assertion, "Lost connection to core, cannot continue."
            "node reset required to re-activate cluster operations.\n");

   GULMD_ASSERT( poller.polls[idx].fd != poller.listenFD , );

   /* if a slave connect, free that too. */
   if( poller.type[idx] == poll_Slave ) {
      remove_slave_from_list(idx);
      recheck_reply_waiters(Slave_bitmask, 0);
   }

   close( poller.polls[idx].fd );
   poller.polls[idx].fd = -1;
   poller.polls[idx].revents = 0; /* clear any other events. */
   poller.state[idx] = poll_Closed;
   poller.type[idx]  = poll_Nothing;
   poller.times[idx] = 0;
   memset(&poller.ipn[idx].ip, 0, sizeof(struct in6_addr));
   if( poller.ipn[idx].name != NULL ) {
      free(poller.ipn[idx].name);
      poller.ipn[idx].name = NULL;
   }
   if( poller.enc[idx] != NULL ) {
      xdr_enc_release(poller.enc[idx]);
      poller.enc[idx] = NULL;
   }
   if( poller.dec[idx] != NULL ) {
      xdr_dec_release(poller.dec[idx]);
      poller.dec[idx] = NULL;
   }
   delete_entire_waiters_list( &poller.outq[idx] );
   totaloutqlen -= poller.outqlen[idx];
   poller.outqlen[idx] = 0;

}

/**
 * close_all_named - 
 * @name: 
 * 
 */
static void close_all_named(char *name)
{
   int i;
   log_msg(lgm_Network2, "Closing all named: %s\n", name);
   for(i=0; i < open_max(); i++) {
      if( poller.ipn[i].name != NULL &&
          strcmp(name, poller.ipn[i].name) == 0 ) {
         close_by_idx(i);
      }
   }
}

/**
 * close_all_clients - 
 */
static void close_all_clients(void)
{
   int i;
   log_msg(lgm_Network2, "Closing all Clients\n");
   for(i=0; i < open_max(); i++) {
      if( poller.type[i] == poll_Client ) {
         close_by_idx(i);
      }
   }
}

/**
 * open_lt_listener - 
 * @port: 
 * 
 * 
 * Returns: int
 */
int open_lt_listener(int port)
{
   int idx;
   poller.listenFD = serv_listen(port);
   if( poller.listenFD < 0 ) return -1;
   idx = add_to_pollers(poller.listenFD, poll_Open, 0, " _accepter_ ",
         &in6addr_any);
   poller.type[idx] = poll_Internal;
   /* no xdr on the listener socket. */
   return 0;
}

/**
 * open_lt_to_core - 
 * @oid: 
 * 
 * 
 * Returns: int
 */
int open_lt_to_core(void)
{
   struct sockaddr_in6 adr;
   int cfd, err, idx;
   uint64_t x_gen;
   uint32_t x_code, x_error, x_rank;
   uint8_t x_ama;

   if((cfd = socket(AF_INET6, SOCK_STREAM, 0)) <0) {
      log_err("Failed to create socket. %d:%s\n", errno, strerror(errno));
      return -1;
   }

   memset(&adr, 0, sizeof(struct sockaddr_in6));
   adr.sin6_family = AF_INET6;
   adr.sin6_addr = in6addr_loopback;
   adr.sin6_port = htons(gulm_config.corePort);

   if( connect(cfd, (struct sockaddr*)&adr, sizeof(struct sockaddr_in6))<0) {
      close(cfd);
      log_err("Failed to connect to core. %d:%s\n", errno, strerror(errno));
      return -1;
   }

   idx = add_to_pollers(cfd, poll_Open, 0, "_ core _", &in6addr_loopback);
   if( idx < 0 ) {
      log_err("Failed to find unsed poller space.\n");
      close_by_idx(idx);
      return -1;
   }
   if( add_xdr_to_poller(idx) < 0 ) {
      log_err("Failed to allocate momeory for xdr.\n");
      close_by_idx(idx);
      return -1;
   }


   do{
      if((err = xdr_enc_uint32(poller.enc[idx], gulm_core_reslgn_req))<0) break;
      if((err = xdr_enc_uint32(poller.enc[idx], GIO_WIREPROT_VERS))<0) break;
      if((err = xdr_enc_string(poller.enc[idx], gulm_config.clusterID))<0)
         break;
      if((err = xdr_enc_string(poller.enc[idx], LTname))<0) break;
      if((err = xdr_enc_uint32(poller.enc[idx], gulm_svc_opt_important)) <0)
         break;
      if((err = xdr_enc_flush(poller.enc[idx]))<0) break;
   }while(0);
   if(err != 0 ) {
      log_err("Failed to send login request to core. %d:%d:%s\n", err, errno,
            strerror(errno));
      close_by_idx(idx);
      return -1;
   }

   /* poll loop is not yet active, so we do the read right here. */

   do{
      if((err = xdr_dec_uint32(poller.dec[idx], &x_code))<0) break;
      if((err = xdr_dec_uint64(poller.dec[idx], &x_gen))<0) break;
      if((err = xdr_dec_uint32(poller.dec[idx], &x_error))<0) break;
      if((err = xdr_dec_uint32(poller.dec[idx], &x_rank))<0) break;
      if((err = xdr_dec_uint8(poller.dec[idx], &x_ama))<0) break;
   }while(0);
   if( err != 0 ) {
      log_err("Failed to receive login reply. %d:%d:%s\n", err, errno, 
            strerror(errno));
      close_by_idx(idx);
      return -1;
   }

   if( x_code != gulm_core_login_rpl ) {
      log_err("Did not get the expected packet in return. got %#x\n", x_code);
      close_by_idx(idx);
      return -1;
   }
   if( x_error != gio_Err_Ok ) {
      log_err("Core returned error %d:%s.\n", x_error, gio_Err_to_str(x_error));
      close_by_idx(idx);
      return -1;
   }

   poller.coreIDX = idx;
   /* yeah! we're hooked up. */
   return 0;
}

/**
 * find_and_cache_idx_for_holder - 
 * @h: <> Holder_t of client we want the poller idx for.
 *
 * Bit of a tricky stunt.  Uses a spot in the Holders_t to cache the last
 * idx that this holder had.  Since in most cases this won't change, but
 * since we do try to keep the state of a client and the state of a TCP/IP
 * socket seperate, there is not a garuntee that this will not change.  The
 * only sane thing we have is the client's name.  And doing a flat loop
 * ssearch for the name *every* time a drop lock request is made (which is
 * nearly every lock op with multiple clients) is unacceptable.  So we
 * search once and cache it.
 * 
 * Returns: poller idx
 */
int find_and_cache_idx_for_holder(Holders_t *h)
{
   int i;

   GULMD_ASSERT( h != NULL, );
   if( h->idx < 0 || h->idx > open_max() ) h->idx = 0;
   if( poller.ipn[h->idx].name != NULL &&
       strcmp(h->name, poller.ipn[h->idx].name) == 0 ) {
      /* valid cached idx. */
      return h->idx;
   }else{
      for(i=0; i < open_max(); i++) {
         if( poller.ipn[i].name != NULL &&
             strcmp(h->name, poller.ipn[i].name) == 0) {
            /* is this poller idx a slave?  (note it is not the name, but
             * the idx we are asking about here. Since the client could
             * have two sockets open to us, once for slave and once for
             * client)
             * If it is, that's not what we want. we are after clients, not
             * slaves.
             */
            if( poller.type[i] == poll_Slave ) continue;
            return ( h->idx = i );
         }
      }
   }
   return -1;
}

/**
 * find_idx_for_name - 
 * @name: 
 * 
 * 
 * Returns: int
 */
int find_idx_for_name(char *name)
{
   int i;

   for(i=0; i < open_max(); i ++ ) {
      if( poller.ipn[i].name != NULL &&
          strcmp(name, poller.ipn[i].name) == 0 ) {
         if( i == get_slave_idx(name) ) continue;
         return i;
      }
   }
   return -1;
}

/*****************************************************************************/

/* random floating prototype...*/
char *lkeytob64(uint8_t *key, uint8_t keylen);

/**
 * queue_lkrq_for_sending - 
 * @idx: 
 * @lkrq: 
 * 
 * 
 * Returns: void
 */
void queue_lkrq_for_sending(int idx, Waiters_t *lkrq)
{
   /* really need these two? */
   LLi_del(&lkrq->wt_list);
   LLi_unhook(&lkrq->wt_list);

   Qu_EnQu(&poller.outq[idx], &lkrq->wt_list);
   poller.outqlen[idx] ++;
   totaloutqlen ++;
   poller.polls[idx].events |= POLLOUT;
}

/**
 * _send_req_update_ - 
 * @idx: 
 * @lkrq: 
 * 
 * 
 * Returns: int
 */
static int _send_req_update_(int idx, Waiters_t *lkrq)
{
   int e;
   xdr_enc_t *enc = poller.enc[idx];
   do {
      if((e=xdr_enc_uint32(enc, gulm_lock_state_updt)) != 0) break;
      if((e=xdr_enc_string(enc, lkrq->name)) != 0) break;
      if((e=xdr_enc_uint64(enc, lkrq->subid)) != 0 ) break;
      if((e=xdr_enc_uint64(enc, lkrq->start)) != 0 ) break;
      if((e=xdr_enc_uint64(enc, lkrq->stop)) != 0 ) break;
      if((e=xdr_enc_raw(enc, lkrq->key, lkrq->keylen)) != 0) break;
      if((e=xdr_enc_uint8(enc, lkrq->state)) != 0) break;
      if((e=xdr_enc_uint32(enc, lkrq->flags)) != 0) break;
      if( lkrq->flags & gio_lck_fg_hasLVB ) {
         if((e=xdr_enc_raw(enc, lkrq->LVB, lkrq->LVBlen)) != 0) break;
      }
      if((e=xdr_enc_flush(enc)) != 0) break;
   }while(0);

   return e;
}

/**
 * send_update_to_slaves - 
 * @req: 
 * 
 */
void send_req_update_to_slaves(Waiters_t *lkrq)
{
   int i;
   Waiters_t *new;

   for(i=0;i<4;i++) {
      if( ! Slaves[i].live ) continue;

      new = duplicate_lkrw(lkrq);
      new->op = gulm_lock_state_updt;
      queue_lkrq_for_sending(Slaves[i].idx, new); 

      /* remember who we sent it to */
      lkrq->Slave_sent |= 1 << (i & 0x7 );
   }
}

/**
 * _send_act_update_ - 
 * @idx: 
 * @lkrq: 
 * 
 * 
 * Returns: int
 */
static int _send_act_update_(int idx, Waiters_t *lkrq)
{
   int e;
   xdr_enc_t *enc = poller.enc[idx];

   do {
      if((e=xdr_enc_uint32(enc, gulm_lock_action_updt)) != 0) break;
      if((e=xdr_enc_string(enc, lkrq->name)) != 0) break;
      if((e=xdr_enc_uint64(enc, lkrq->subid)) != 0 ) break;
      if((e=xdr_enc_raw(enc, lkrq->key, lkrq->keylen)) != 0) break;
      if((e=xdr_enc_uint8(enc, lkrq->state)) != 0) break;
      if( lkrq->state == gio_lck_st_SyncLVB ) {
         if((e=xdr_enc_raw(enc, lkrq->LVB, lkrq->LVBlen)) != 0) break;
      }
      if((e=xdr_enc_flush(enc)) != 0) break;
   }while(0);
   return e;
}

/**
 * send_act_update_to_slaves - 
 * @lkrq: 
 * 
 * 
 * Returns: void
 */
void send_act_update_to_slaves(Waiters_t *lkrq)
{
   int i;
   Waiters_t *new;

   for(i=0;i<4;i++) {
      if( ! Slaves[i].live ) continue;
       log_msg(lgm_LockUpdates, "Gonna send lock action update to %s "
               "about %s act:%#x\n",
               poller.ipn[Slaves[i].idx].name,
               lkeytob64(lkrq->key, lkrq->keylen),  lkrq->state);

      new = duplicate_lkrw(lkrq);
      new->op = gulm_lock_action_updt;
      queue_lkrq_for_sending(Slaves[i].idx, new); 

      /* remember who we sent it to */
      lkrq->Slave_sent |= 1 << (i & 0x7 );
   }
}


/**
 * _send_update_reply_ - 
 * @idx: 
 * @lkrq: 
 * 
 * 
 * Returns: int
 */
static int _send_update_reply_(int idx, Waiters_t *lkrq)
{
   int err;
   xdr_enc_t *enc = poller.enc[idx];
   do{
      if( (err=xdr_enc_uint32(enc, gulm_lock_update_rpl)) != 0 ) break;
      if( (err=xdr_enc_raw(enc, lkrq->key, lkrq->keylen)) != 0 ) break;
      if( (err=xdr_enc_flush(enc)) != 0 ) break;
   }while(0);
   return err;
}

/**
 * send_update_reply_to_master - 
 * @lkrq: 
 * 
 * 
 * Returns: int
 */
void send_update_reply_to_master(Waiters_t *lkrq)
{
   Waiters_t *new;

   GULMD_ASSERT( poller.MasterIDX != -1 , );

    log_msg(lgm_LockUpdates, "Gonna send update reply to Master %s "
          "about %s\n",
            poller.ipn[poller.MasterIDX].name,
            lkeytob64(lkrq->key, lkrq->keylen));
   
   new = duplicate_lkrw(lkrq);
   new->op = gulm_lock_update_rpl;
   queue_lkrq_for_sending(poller.MasterIDX, new); 
}

/**
 * _send_drop_exp_to_slave_ - 
 * @idx: 
 * @lkrq: 
 * 
 * 
 * Returns: int
 */
static int _send_drop_exp_to_slave_(int idx, Waiters_t *lkrq)
{
   int e;
   xdr_enc_t *enc = poller.enc[idx];
   do{
      if((e=xdr_enc_uint32(enc, gulm_lock_drop_exp)) != 0 ) break;
      if((e=xdr_enc_string(enc, lkrq->name)) != 0 ) break;
      if((e=xdr_enc_raw(enc, lkrq->key, lkrq->keylen)) != 0 ) break;
      if((e=xdr_enc_flush(enc)) != 0 ) break;
   }while(0);
   return e;
}

/**
 * send_drop_exp_to_slaves - 
 * @name: 
 * 
 * FIXME Handle NULLs!!!!
 * 
 * Returns: void
 */
static void send_drop_exp_to_slaves(char *name, uint8_t *mask, uint16_t len)
{
   int i;
   Waiters_t *lkrq;

   for(i=0;i<4;i++) {
      if( ! Slaves[i].live ) continue;

      lkrq = get_new_lkrq();
      GULMD_ASSERT( lkrq != NULL, );
      lkrq->op = gulm_lock_drop_exp;
      if( name != NULL ) {
         lkrq->name = strdup(name);
         GULMD_ASSERT(lkrq->name != NULL , );
      }
      else lkrq->name = NULL;

      if( mask != NULL ) {
         lkrq->key = malloc(len);
         GULMD_ASSERT(lkrq->key != NULL , );
         memcpy(lkrq->key, mask, len);
         lkrq->keylen = len;
      } else {
         lkrq->key = NULL;
         lkrq->keylen = 0;
      }

      queue_lkrq_for_sending(Slaves[i].idx, lkrq); 

   }
}

/**
 * _send_lk_act_reply_ - 
 * @idx: 
 * @lkrq: 
 * 
 * 
 * Returns: void
 */
static int _send_lk_act_reply_(int idx, Waiters_t *lkrq)
{
   int err;
   xdr_enc_t *enc = poller.enc[idx];
   do{
      if((err=xdr_enc_uint32(enc, gulm_lock_action_rpl)) != 0 ) break;
      if((err=xdr_enc_raw(enc, lkrq->key, lkrq->keylen)) != 0 ) break;
      if((err=xdr_enc_uint64(enc, lkrq->subid)) != 0 ) break;
      if((err=xdr_enc_uint8(enc, lkrq->state)) != 0 ) break;
      if((err=xdr_enc_uint32(enc, lkrq->ret)) != 0 ) break;
      if((err=xdr_enc_flush(enc)) != 0 ) break;
   }while(0);
   return err;
}

/**
 * send_act_lk_reply - 
 * @lkrq: 
 * @lk: 
 * @retcode: 
 * 
 * sends a reply to the proper client.  Also determines if they need the
 * LVB attached.
 *
 * XXX
 * I think I'm losing the lkrq if there are errors.
 * 
 * Returns: int
 */
int send_act_lk_reply(Waiters_t *lkrq, uint32_t retcode)
{
   /*
    * Look up the idx in the pollers, and compair names to make sure this
    * is the correct one.  Then use that encoder.
    * If not the correct one, scan the pollers for the correct one.
    * If we don't find the correct one, then what?
    */
   if( lkrq->idx < 0 || lkrq->idx > open_max() ) lkrq->idx = 0;
   /* the strcmp slows things down a tiny bit.
    * wonder if there is another way.
    */
   if( poller.ipn[lkrq->idx].name == NULL ||
       strcmp( lkrq->name, poller.ipn[lkrq->idx].name) != 0 ) {
      if( (lkrq->idx = find_idx_for_name(lkrq->name)) < 0 ) {
         log_err("No encoder for \"%s\"! lock:%s",
               lkrq->name, lkeytob64(lkrq->key, lkrq->keylen));
         /* i *am* losing the replies here.  FIXME
          * shit.
          *
          * So I need to queue this somehow.
          *
          * bleh. Outgoing queue on each lock?  icky.
          * Normally, there will never be anything on this queue for any
          * real length of time.  So, I could have a single process wide
          * outgoing queue.
          *
          * Work it like the dropreq queue, when the node gets logged in,
          * flush.
          *
          * Umm, will that break ordering?
          *
          * Uh, where have I seen this when there have not been other
          * errors?
          */
         return -1;
      }
   }

   lkrq->ret = retcode;
   lkrq->op = gulm_lock_action_rpl;
   queue_lkrq_for_sending(lkrq->idx, lkrq); 

   return 0;
}

/**
 * _send_lk_req_reply_ - 
 * @idx: 
 * @lkrq: 
 * 
 * 
 * Returns: int
 */
static int _send_lk_req_reply_(int idx, Waiters_t *lkrq)
{
   int err;
   xdr_enc_t *enc = poller.enc[idx];
   do{
      if((err=xdr_enc_uint32(enc, gulm_lock_state_rpl)) != 0 ) break;
      if((err=xdr_enc_raw(enc, lkrq->key, lkrq->keylen)) != 0 ) break;
      if((err=xdr_enc_uint64(enc, lkrq->subid)) != 0 ) break;
      if((err=xdr_enc_uint64(enc, lkrq->start)) != 0 ) break;
      if((err=xdr_enc_uint64(enc, lkrq->stop)) != 0 ) break;
      if((err=xdr_enc_uint8(enc, lkrq->state)) != 0 ) break;
      if((err=xdr_enc_uint32(enc, lkrq->flags)) != 0 ) break;
      if((err=xdr_enc_uint32(enc, lkrq->ret)) != 0 ) break;
      if( lkrq->flags & gio_lck_fg_hasLVB ) {
            lvb_log_msg("For %s, Lock %s: Sent LVB (%d) %s\n", lkrq->name,
               lkeytob64(lkrq->key, lkrq->keylen), lkrq->LVBlen,
               lvbtohex(lkrq->LVB, lkrq->LVBlen));
         if((err=xdr_enc_raw(enc, lkrq->LVB, lkrq->LVBlen)) != 0 ) break;
      }
      if((err=xdr_enc_flush(enc)) != 0 ) break;
   }while(0);
   return err;
}

/**
 * send_req_lk_reply - 
 * @lkrq: 
 * @lk: 
 * @retcode: 
 * 
 * sends a reply to the proper client.  Also determines if they need the
 * LVB attached.
 *
 * 
 * Returns: int
 */
int send_req_lk_reply(Waiters_t *lkrq, Lock_t *lk, uint32_t retcode)
{
   Waiters_t *new;

   /*
    * Look up the idx in the pollers, and compair names to make sure this
    * is the correct one.  Then use that encoder.
    * If not the correct one, scan the pollers for the correct one.
    * If we don't find the correct one, then what?
    */
   if( lkrq->idx < 0 || lkrq->idx > open_max() ) lkrq->idx = 0;
   /* the strcmp slows things down a tiny bit.
    * wonder if there is another way.
    */
   if( poller.ipn[lkrq->idx].name == NULL ||
       strcmp( lkrq->name, poller.ipn[lkrq->idx].name) != 0 ) {
      if( (lkrq->idx = find_idx_for_name(lkrq->name)) < 0 ) {
         log_err("No encoder for \"%s\"! lock:%s",
               lkrq->name, lkeytob64(lkrq->key, lkrq->keylen));
         return -1;
      }
   }

   lkrq->ret = retcode;

   new = duplicate_lkrw(lkrq);

   /* ok, now package up the reply. */
   if( retcode == gio_Err_Ok &&
       lk != NULL &&
       lkrq->state != gio_lck_st_Unlock &&
       lk->LVBlen > 0 &&
       lk->LVB != NULL )
   {
      new->flags |= gio_lck_fg_hasLVB;
      if( new->LVB != NULL ) free(new->LVB);
      new->LVB = malloc(lk->LVBlen);
      memcpy(new->LVB, lk->LVB, lk->LVBlen);
      new->LVBlen = lk->LVBlen;
   } else {
      /* no lvb. */
      new->flags &= ~gio_lck_fg_hasLVB;
   }

   new->op = gulm_lock_state_rpl;
   queue_lkrq_for_sending(lkrq->idx, new); 

   /* all done with this. */
#ifdef LOCKHISTORY
   record_lkrq(lk, lkrq);
#else
   recycle_lkrq(lkrq);
#endif

   return 0;
}

/**
 * _send_query_reply_ - 
 * @idx: 
 * @lkrq: 
 * 
 * 
 * Returns: int
 */
static int _send_query_reply_(int idx, Waiters_t *lkrq)
{
   int err;
   LLi_t *tp;
   Holders_t *h;
   xdr_enc_t *enc = poller.enc[idx];
   do{
      if((err=xdr_enc_uint32(enc, gulm_lock_query_rpl)) != 0 ) break;
      if((err=xdr_enc_raw(enc, lkrq->key, lkrq->keylen)) != 0 ) break;
      if((err=xdr_enc_uint64(enc, lkrq->subid)) != 0 ) break;
      if((err=xdr_enc_uint64(enc, lkrq->start)) != 0 ) break;
      if((err=xdr_enc_uint64(enc, lkrq->stop)) != 0 ) break;
      if((err=xdr_enc_uint8(enc, lkrq->state)) != 0 ) break;
      if((err=xdr_enc_uint32(enc, lkrq->ret)) != 0 ) break;
      /* holder[s] info follows. */
      if((err = xdr_enc_list_start(enc)) != 0 ) return err;
      for(tp = LLi_next(&lkrq->holders);
          NULL != LLi_data(tp);
          tp = LLi_next(tp) )
      {
         h = LLi_data(tp);
         if((err = xdr_enc_string(enc, h->name)) != 0 ) return err;
         if((err = xdr_enc_uint64(enc, h->subid)) != 0 ) return err;
         if((err = xdr_enc_uint64(enc, h->start)) != 0 ) return err;
         if((err = xdr_enc_uint64(enc, h->stop)) != 0 ) return err;
         if((err = xdr_enc_uint8(enc, h->state)) != 0 ) return err;
      }
      if((err = xdr_enc_list_stop(enc)) != 0 ) return err;
      if((err=xdr_enc_flush(enc)) != 0 ) break;
   }while(0);
   return err;
}

/**
 * send_query_reply - 
 * @lkrq: 
 * 
 * 
 * Returns: int
 */
int send_query_reply(Waiters_t *lkrq, uint32_t retcode)
{
   /* make sure the right poller is being used. */
   if( lkrq->idx < 0 || lkrq->idx > open_max() ) lkrq->idx = 0;
   if( poller.ipn[lkrq->idx].name == NULL ||
       strcmp( lkrq->name, poller.ipn[lkrq->idx].name) != 0 ) {
      if( (lkrq->idx = find_idx_for_name(lkrq->name)) < 0 ) {
         log_err("No encoder for \"%s\"! lock:%s",
               lkrq->name, lkeytob64(lkrq->key, lkrq->keylen));
         return -1;
      }
   }
   lkrq->ret = retcode;
   lkrq->op = gulm_lock_query_rpl;
   queue_lkrq_for_sending(lkrq->idx, lkrq);
   return 0;
}

/**
 * _send_drp_req_ - 
 * @idx: 
 * @lkrq: 
 * 
 * 
 * Returns: int
 */
static int _send_drp_req_(int idx, Waiters_t *lkrq)
{
   int err;
   xdr_enc_t *enc = poller.enc[idx];
#ifdef TIMECALLBACKS
   struct timeval tv;
   gettimeofday(&tv, NULL);

   /* TODO
    * measure the amount of time between successive calls to this function.
    * See if there is a delay there.  Would imagin so.  just want to check.
    */
#endif
   do {
      if((err=xdr_enc_uint32(enc, gulm_lock_cb_state)) !=0 ) break;
      if((err=xdr_enc_raw(enc, lkrq->key, lkrq->keylen)) !=0 ) break;
      if((err=xdr_enc_uint64(enc, lkrq->subid)) != 0 ) break;
      if((err=xdr_enc_uint8(enc, lkrq->state)) !=0 ) break;
#ifdef TIMECALLBACKS
      if((err=xdr_enc_uint64(enc, tvs2uint64(tv))) != 0) break;
#endif
      if((err=xdr_enc_flush(enc)) !=0 ) break;
   }while(0);
   return err;
}

/**
 * send_drp_req - 
 * @Cname: < skip this one.
 * @lk: <
 * @DesireState: <
 * 
 * 
 */
void send_drp_req(Lock_t *lk, Waiters_t *lkrq)
{
   int idx;
   LLi_t *tp;
   Holders_t *h;
   Waiters_t *new;

   if( ! LLi_empty( &lk->Holders ) ) {
      for(tp=LLi_next(&lk->Holders); LLi_data(tp) != NULL; tp=LLi_next(tp)) {
         h = LLi_data(tp);
         if( !(h->flags & gio_lck_fg_NoCallBacks) &&
             ! compare_holder_waiter_names(h, lkrq) ) {

            new = get_new_lkrq();
            GULMD_ASSERT( new != NULL, );
            new->op = gulm_lock_cb_state;
            new->name = strdup(h->name);
            GULMD_ASSERT( new->name != NULL, );
            new->subid = h->subid;
            new->keylen = lk->keylen;
            new->key = malloc(lk->keylen);
            GULMD_ASSERT( new->key != NULL, );
            memcpy(new->key, lk->key, lk->keylen);
            new->state = lkrq->state; /* which state we'd like */

            if( (idx = find_and_cache_idx_for_holder(h)) > 0 ) {
               queue_lkrq_for_sending(idx, new); 

            }else{
               log_msg(lgm_locking, "Client Node %s not currently logged in. "
                     "Queuing Droplok Request for later.\n", h->name);

               LLi_add_after(&DropRequestPlaybackQueue, &new->wt_list);
               cnt_drpq ++;

            }

         }/*if( strcmp(h->name, name) != 0 )*/
      }/*for()*/
   }/*if( ! LLi_empty( &lk->Holders ) )*/
}

/**
 * playback_droprequests - 
 * @idx: 
 * @name: 
 * 
 * 
 * Returns: void
 */
void playback_droprequests(int idx, uint8_t *name)
{
   LLi_t *tmp, *nxt;
   Waiters_t *lkrq;

   for(tmp = LLi_next(&DropRequestPlaybackQueue);
       LLi_data(tmp) != NULL;
       tmp = nxt) {
      nxt = LLi_next(tmp);
      lkrq = LLi_data(tmp);
      if( strcmp(name, lkrq->name) == 0 ) {
         LLi_del(tmp);
         cnt_drpq --;

         log_msg(lgm_locking, "Playing back a drop lock request for new client"
               " %s\n", lkrq->name);

         /* move from this list to the out queue. */
         queue_lkrq_for_sending(idx, lkrq); 
      }
   }
}

/**
 * expire_queued_dropreqs - 
 * @name: 
 * 
 * if client expires, all of its locks will be freed. which will
 * effectively do that same as handling these drop reqs.
 * 
 */
void expire_queued_dropreqs(uint8_t *name)
{
   LLi_t *tmp, *nxt;
   Waiters_t *lkrq;

   for(tmp = LLi_next(&DropRequestPlaybackQueue);
       LLi_data(tmp) != NULL;
       tmp = nxt) {
      nxt = LLi_next(tmp);
      lkrq = LLi_data(tmp);
      if( strcmp(name, lkrq->name) == 0 ) {
         LLi_del(tmp);
         cnt_drpq --;
         /* now free it */
         recycle_lkrq(lkrq);
      }
   }
}

/**
 * _send_drop_all_req_ - 
 * @idx: 
 * @lkrq: 
 * 
 * 
 * Returns: int
 */
static int _send_drop_all_req_(int idx, Waiters_t *lkrq)
{
   int err;
   xdr_enc_t *enc = poller.enc[idx];
   do{
      if((err = xdr_enc_uint32(enc, gulm_lock_cb_dropall))!=0) break;
      if((err = xdr_enc_flush(enc))!=0) break;
   }while(0);
   return err;
}

/**
 * send_drop_all_req - 
 * 
 * sends to all the currently connected clients.
 */
void send_drop_all_req(void)
{
   int idx;
   Waiters_t *lkrq;

   for(idx=0; idx < poller.maxi; idx++ ) {
      if( poller.type[idx] == poll_Client ) {
         lkrq = get_new_lkrq();
         lkrq->op = gulm_lock_cb_dropall;
         queue_lkrq_for_sending(idx, lkrq); 
      }
   }
}

/*****************************************************************************/
static int send_io_stats(xdr_enc_t *enc)
{
   struct timeval tv;
   char tmp[256] = "3: Well trust me, there is nothing in here.";
   int err;

   if((err=xdr_enc_string(enc, "I_am")) != 0 ) return err;
   if((err=xdr_enc_string(enc, gio_I_am_to_str(I_am_the))) != 0 ) return err;

   if( MasterIN.name != NULL ) {
      if((err=xdr_enc_string(enc, "Master")) != 0 ) return err;
      if((err=xdr_enc_string(enc, MasterIN.name)) != 0 ) return err;

      xdr_enc_string(enc, "login");
      if( poller.MasterIDX >=0 && logging_into_master ) {
         xdr_enc_string(enc, "Trying");
      }else
      if( poller.MasterIDX >= 0 ) {
         xdr_enc_string(enc, "In");
      }else
      {
         xdr_enc_string(enc, "Out");
      }

   }

   gettimeofday(&tv, NULL);
   xdr_enc_string(enc, "run time");
   snprintf(tmp, 256, "%lu", tv.tv_sec - Started_at.tv_sec );
   xdr_enc_string(enc, tmp);

   xdr_enc_string(enc, "pid");
   snprintf(tmp, 256, "%u", getpid());
   xdr_enc_string(enc, tmp);

   xdr_enc_string(enc, "verbosity");
   get_verbosity_string(tmp, 256, verbosity);
   xdr_enc_string(enc, tmp);

   xdr_enc_string(enc, "id");
   snprintf(tmp, 256, "%u", LTid);
   xdr_enc_string(enc, tmp);

   xdr_enc_string(enc, "partitions");
   snprintf(tmp, 256, "%u", gulm_config.how_many_lts);
   xdr_enc_string(enc, tmp);

   xdr_enc_string(enc, "out_queue");
   snprintf(tmp, 256, "%lu", totaloutqlen);
   xdr_enc_string(enc, tmp);

   xdr_enc_string(enc, "drpb_queue");
   snprintf(tmp, 256, "%lu", cnt_drpq);
   xdr_enc_string(enc, tmp);

   /* xdr_enc_flush is called by this function's caller. */
   return 0;
}


static int accept_connection(void)
{
   int clisk, i;
   struct sockaddr_in6 adr;

   i = sizeof(struct sockaddr_in6);
   if( (clisk = accept(poller.listenFD, (struct sockaddr*)&adr, &i)) <0) {
      log_err("error in accept: %s", strerror(errno));
      return -1;
   }

   if( set_opts(clisk) <0) {
      log_err("Cannot set socket options for new connection. Killing it.\n");
      close(clisk);
      return -1;
   }

   i = add_to_pollers(clisk, poll_Accepting, tvs2uint64(NOW),
                      ip6tostr(&adr.sin6_addr), &adr.sin6_addr);
   if( i < 0 ) {
      log_err("Failed to add new socket to poller list. %s\n", strerror(errno));
      close(clisk);
      return -1;
   }
   if( add_xdr_to_poller(i) != 0 ) {
      log_err("Failed to attatch xdr to new socket do to lack of memory.\n");
      close_by_idx(i);
      return -1;
   }

   return 0;
}

/**
 * logintoMaster - 
 * 
 * Returns: int
 */
static int logintoMaster(void)
{
   struct sockaddr_in6 adr;
   int i,cmFD,err;
   xdr_enc_t *xdr;

   log_msg(lgm_LoginLoops, "Trying to log into Master %s\n",
         print_ipname(&MasterIN));

   /* socket connect to CM */
   if((cmFD = socket(AF_INET6, SOCK_STREAM, 0)) <0){
      log_err("Failed to create socket. %s\n", strerror(errno));
      return -1;
   }

   memset(&adr, 0, sizeof(struct sockaddr_in6));
   adr.sin6_family = AF_INET6;
   memcpy(&adr.sin6_addr, &MasterIN.ip, sizeof(struct in6_addr));
   adr.sin6_port = htons( gulm_config.lt_port + LTid );

   if( connect(cmFD, (struct sockaddr*)&adr, sizeof(struct sockaddr_in6))<0) {
      close(cmFD);
      log_msg(lgm_LoginLoops, "Cannot connect to %s (%s)\n",
            print_ipname(&MasterIN), strerror(errno));
      return -1;
      /* need to go to next here */
   }

   if( set_opts(cmFD) <0) {
      close(cmFD);
      log_msg(lgm_LoginLoops, "Failed to set options (%s)\n", strerror(errno));
      return -1;
   }

   /* */
   i = add_to_pollers(cmFD, poll_Trying, tvs2uint64(NOW),
                      MasterIN.name, &MasterIN.ip);
   if( i < 0 ) { /* out of free FDs. */
      log_err("Failed to find unused poller space.\n");
      close_by_idx(i);
      return -1;
   }
   if( add_xdr_to_poller(i) < 0 ) {
      log_err("Failed to allocate memory for xdr.\n");
      close_by_idx(i);
      return -1;
   }

   /* send login request */
   xdr = poller.enc[i];

   do {
      if((err = xdr_enc_uint32(xdr, gulm_lock_login_req)) != 0) break;
      if((err = xdr_enc_uint32(xdr, GIO_WIREPROT_VERS)) != 0) break;
      if((err = xdr_enc_string(xdr, myName)) != 0) break;
      if((err = xdr_enc_uint8(xdr, gio_lck_st_Slave)) != 0) break;
      if((err = xdr_enc_flush(xdr)) != 0) break;
   }while(0);
   if( err != 0 ) {
      log_msg(lgm_LoginLoops, "Errors trying to send login request. %d:%s\n",
            err, strerror(errno));
      close_by_idx(i);
      return -1;
   }

   logging_into_master = TRUE;

   return 0;
}

/**
 * recv_Masterlogin_reply - 
 * @idx: 
 * 
 * 
 * Returns: int
 */
static int recv_Masterlogin_reply(int idx)
{
   xdr_dec_t *xdr = poller.dec[idx];
   uint32_t code=~0;
   uint32_t rpl_err=~0;
   uint8_t rpl_ama=~0;
   int err;

   /* recv login reply */
   do{
      if((err = xdr_dec_uint32(xdr, &code)) != 0) break;
      if((err = xdr_dec_uint32(xdr, &rpl_err)) != 0) break;
      if((err = xdr_dec_uint8(xdr, &rpl_ama)) != 0) break;
   } while(0);
   if( err != 0 ) {
      log_err("Errors trying to login to Master: (%s idx:%d fd:%d) %d:%s\n",
            print_ipname(&poller.ipn[idx]),
            idx, poller.polls[idx].fd,
            err, strerror(errno));
      goto exit;
   }

   if( rpl_err != 0 ) {
      log_err("Errors trying to login to Master: (%s) %d:%s\n",
            print_ipname(&poller.ipn[idx]),
            rpl_err, gio_Err_to_str(rpl_err));
      err = rpl_err;
      goto exit;
   }

   PartialLockspace = TRUE;
   /* get current state */
   if( (err=deserialize_lockspace(poller.polls[idx].fd)) != 0 ) {
      log_err("Failed to deserialize initial lockspace from Master"
              " (%d:%d:%s)\n", err, errno, strerror(errno));
      goto exit;
   }
   PartialLockspace = FALSE;

   poller.MasterIDX = idx;
   poller.state[idx] = poll_Open;
   poller.type[idx] = poll_Internal;/*not really, but close enough*/
   poller.times[idx] = 0;
   log_msg(lgm_Network, "Logged into Master at %s\n", print_ipname(&MasterIN));

exit:
   logging_into_master = FALSE;

   return err;
}

/*****************************************************************************/


/**
 * send_some_data - 
 * @idx: 
 * 
 * 
 * Returns: int
 */
int send_some_data(int idx)
{
   LLi_t *tmp;
   Waiters_t *lkrq;
   int err=0;

   if( !Qu_empty(&poller.outq[idx]) ) {
      tmp = Qu_DeQu(&poller.outq[idx]);
      lkrq = Qu_data(tmp);
      poller.outqlen[idx] --;
      totaloutqlen --;
      switch(lkrq->op) {
         case gulm_lock_state_updt:
            err = _send_req_update_(idx, lkrq);
            break;
         case gulm_lock_action_updt:
            err = _send_act_update_(idx, lkrq);
            break;
         case gulm_lock_update_rpl:
            err = _send_update_reply_(idx, lkrq);
            break;
         case gulm_lock_drop_exp:
            err = _send_drop_exp_to_slave_(idx, lkrq);
            break;
         case gulm_lock_action_rpl:
            err = _send_lk_act_reply_(idx, lkrq);
            break;
         case gulm_lock_state_rpl:
            err = _send_lk_req_reply_(idx, lkrq);
            break;
         case gulm_lock_cb_state:
            err = _send_drp_req_(idx, lkrq);
            break;
         case gulm_lock_cb_dropall:
            err = _send_drop_all_req_(idx, lkrq);
            break;
         case gulm_lock_query_rpl:
            err = _send_query_reply_(idx, lkrq);
            break;

         default:
            log_err("Cannot send packet type %#x:%s !\n",
                  lkrq->op, gio_opcodes(lkrq->op));
            break;
      }
      if( err != 0 ) {
         log_err("Warning! When trying to send a %#x:%s packet, we got a "
               "%d:%d:%s\n", lkrq->op, gio_opcodes(lkrq->op),
               err, errno, strerror(errno));
      }
      recycle_lkrq(lkrq);
   }

   if( Qu_empty(&poller.outq[idx]) ) poller.polls[idx].events &= ~POLLOUT;
   return err;
}

/*****************************************************************************/

/**
 * pack_lkrq_from_io - 
 * @lkrq: 
 * @code: 
 * @dec: 
 * @enc: 
 * 
 * Returns: int
 */
int pack_lkrq_from_io(Waiters_t *lkrq, uint32_t code,
      xdr_dec_t *dec, int idx)
{
   int err = 0;
   uint8_t *x_name=NULL;
   LLi_init( &lkrq->wt_list, lkrq);
   lkrq->op = code;
   lkrq->idx = idx;
   if( gulm_lock_state_req == code ) {
      do {
         lkrq->name = strdup(poller.ipn[idx].name);
         if( lkrq->name == NULL ) { err = -ENOMEM; break; }
         if((err = xdr_dec_raw_m(dec, (void**)&lkrq->key, &lkrq->keylen)) != 0 )
            break;
         if((err = xdr_dec_uint64(dec, &lkrq->subid)) != 0 ) break;
         if((err = xdr_dec_uint64(dec, &lkrq->start)) != 0 ) break;
         if((err = xdr_dec_uint64(dec, &lkrq->stop)) != 0 ) break;
         if((err = xdr_dec_uint8(dec, &lkrq->state)) != 0 ) break;
         if((err = xdr_dec_uint32(dec, &lkrq->flags)) != 0 ) break;
         if( lkrq->flags & gio_lck_fg_hasLVB ) {
            if((err = xdr_dec_raw_m(dec, (void**)&lkrq->LVB, &lkrq->LVBlen))!=0)
               break;
         }else{
            lkrq->LVB = NULL;
            lkrq->LVBlen = 0;
         }
      }while(0);
   }else
   if( gulm_lock_state_updt == code ) {
      do {
         if((err = xdr_dec_string(dec, &lkrq->name)) != 0 ) break;
         if((err = xdr_dec_uint64(dec, &lkrq->subid)) != 0 ) break;
         if((err = xdr_dec_uint64(dec, &lkrq->start)) != 0 ) break;
         if((err = xdr_dec_uint64(dec, &lkrq->stop)) != 0 ) break;
         if((err = xdr_dec_raw_m(dec, (void**)&lkrq->key, &lkrq->keylen)) != 0 )
            break;
         if((err = xdr_dec_uint8(dec, &lkrq->state)) != 0 ) break;
         if((err = xdr_dec_uint32(dec, &lkrq->flags)) != 0 ) break;
         if( lkrq->flags & gio_lck_fg_hasLVB ) {
            if((err = xdr_dec_raw_m(dec, (void**)&lkrq->LVB, &lkrq->LVBlen))!=0)
               break;
         }else{
            lkrq->LVB = NULL;
            lkrq->LVBlen = 0;
         }
      }while(0);
   }else
   if( gulm_lock_action_req == code ) {
      do {
         lkrq->name = strdup(poller.ipn[idx].name);
         if( lkrq->name == NULL ) { err = -ENOMEM; break; }
         if((err = xdr_dec_raw_m(dec, (void**)&lkrq->key, &lkrq->keylen)) != 0 )
            break;
         if((err = xdr_dec_uint64(dec, &lkrq->subid)) != 0 ) break;
         if((err = xdr_dec_uint8(dec, &lkrq->state)) != 0 ) break;
         if( lkrq->state == gio_lck_st_SyncLVB ) {
            if((err = xdr_dec_raw_m(dec, (void**)&lkrq->LVB, &lkrq->LVBlen))!=0)
               break;
         }else{
            lkrq->LVB = NULL;
            lkrq->LVBlen = 0;
         }
      }while(0);
   }else
   if( gulm_lock_action_updt == code ) {
      do {
         if((err = xdr_dec_string(dec, &lkrq->name)) != 0 ) break;
         if((err = xdr_dec_uint64(dec, &lkrq->subid)) != 0 ) break;
         if((err = xdr_dec_raw_m(dec, (void**)&lkrq->key, &lkrq->keylen)) != 0 )
            break;
         if((err = xdr_dec_uint8(dec, &lkrq->state)) != 0 ) break;
         if( lkrq->state == gio_lck_st_SyncLVB ) {
            if((err = xdr_dec_raw_m(dec, (void**)&lkrq->LVB, &lkrq->LVBlen))!=0)
               break;
         }else{
            lkrq->LVB = NULL;
            lkrq->LVBlen = 0;
         }
      }while(0);
   }else
   if( gulm_lock_query_req == code ) {
      do {
         lkrq->name = strdup(poller.ipn[idx].name);
         if( lkrq->name == NULL ) { err = -ENOMEM; break; }
         if((err = xdr_dec_raw_m(dec, (void**)&lkrq->key, &lkrq->keylen)) != 0 )
            break;
         if((err = xdr_dec_uint64(dec, &lkrq->subid)) != 0 ) break;
         if((err = xdr_dec_uint64(dec, &lkrq->start)) != 0 ) break;
         if((err = xdr_dec_uint64(dec, &lkrq->stop)) != 0 ) break;
         if((err = xdr_dec_uint8(dec, &lkrq->state)) != 0 ) break;
      }while(0);
   }else
   {
      err = -1;
      log_err("Bad code!\n");
   }
   if( x_name != NULL ) free(x_name);
   return err;
}

/**
 * do_login - 
 * @idx: 
 * 
 * Slaves can connect to me when I am Master or Arbit.
 * Clients can only connect when I am Master.
 *
 * Returns: void
 */
static void do_login(int idx)
{
   xdr_dec_t *dec = poller.dec[idx];
   xdr_enc_t *enc = poller.enc[idx];
   uint32_t x_proto;
   uint8_t *x_name = NULL;
   uint8_t x_ama;
   int err, soff;

   do {
      if((err = xdr_dec_uint32(dec, &x_proto)) != 0) break;
      if(GIO_WIREPROT_VERS != x_proto) {err=gio_Err_BadWireProto; break;}
      if((err = xdr_dec_string(dec, &x_name)) != 0) break;
      if((err = xdr_dec_uint8(dec, &x_ama)) != 0) break;
   }while(0);
   if(err!=0) {
      if( x_name != NULL ) {free(x_name); x_name = NULL;}
      close_by_idx(idx);
      return;
   }

   if( !validate_nodel(nodelists, x_name, &poller.ipn[idx].ip) ) {
      do{
         if(xdr_enc_uint32(enc, gulm_lock_login_rpl) != 0) break;
         if(xdr_enc_uint32(enc, gio_Err_NotAllowed) != 0) break;
         if(xdr_enc_uint8(enc, I_am_the) != 0) break;
         if(xdr_enc_flush(enc) != 0) break;
      }while(0);
      close_by_idx(idx);
   }else
   if( gio_lck_st_Slave == x_ama ) {
      if( gio_Mbr_ama_Master == I_am_the ||
          gio_Mbr_ama_Arbitrating == I_am_the ) {
         if( add_to_slavelist(idx, x_name ) != 0 ) {
            do {
               if(xdr_enc_uint32(enc, gulm_lock_login_rpl) != 0) break;
               if(xdr_enc_uint32(enc, gio_Err_MemoryIssues) != 0) break;
               if(xdr_enc_uint8(enc, I_am_the) != 0) break;
               if(xdr_enc_flush(enc) != 0) break;
            }while(0);
            close_by_idx(idx);
         } else {

            do {
               if((err=xdr_enc_uint32(enc, gulm_lock_login_rpl)) != 0) break;
               if((err=xdr_enc_uint32(enc, gio_Err_Ok)) != 0) break;
               if((err=xdr_enc_uint8(enc, I_am_the)) != 0) break;
               if((err=xdr_enc_flush(enc)) != 0) break;
            }while(0);
            if(err != 0 ) {
               log_msg(lgm_Network,
                     "Errors %d:%s trying to send login reply to fd:%d, %s\n",
                     err, strerror(errno), poller.polls[idx].fd, 
                     poller.ipn[idx].name);
               remove_slave_from_list(idx);
               close_by_idx(idx);
               if( x_name != NULL ) {free(x_name); x_name = NULL;}
               return;
            }

            if((err=serialize_lockspace( poller.polls[idx].fd )) != 0) {
               log_msg(lgm_Network,
                     "Errors '%d:%d:%s' serializing lock space to idx:%d "
                     "fd:%d, %s\n",
                     err, errno, strerror(errno), idx, poller.polls[idx].fd, 
                     poller.ipn[idx].name);
               remove_slave_from_list(idx);
               close_by_idx(idx);
               if( x_name != NULL ) {free(x_name); x_name = NULL;}
               return;
            }

            if( poller.ipn[idx].name != NULL ) free( poller.ipn[idx].name );
            poller.ipn[idx].name = x_name;
            poller.state[idx] = poll_Open;
            poller.type[idx]  = poll_Slave;
            poller.times[idx] = 0;
            soff = get_slave_offset(idx);
            log_msg(lgm_Network, "Attached slave %s idx:%d fd:%d "
                  "(soff:%d connected:%#x)\n",
                  print_ipname(&poller.ipn[idx]),
                  idx, poller.polls[idx].fd,
                  soff, Slave_bitmask);

            recheck_reply_waiters(Slave_bitmask, 1<<soff );
            /*
             * When Slave foo logs in, we scan lockspace
             * and if we find any reply_waiters that claim to have sent to
             * them, but have not received a reply, we mark that reply.
             */
            return;
         }
      }else{
         do {
            if(xdr_enc_uint32(enc, gulm_lock_login_rpl) != 0) break;
            if(xdr_enc_uint32(enc, gio_Err_NotAllowed) != 0) break;
            if(xdr_enc_uint8(enc, gio_Mbr_ama_Slave) != 0) break;
            if(xdr_enc_flush(enc) != 0) break;
         }while(0);
         close_by_idx(idx);
      }
   }else
   if( gio_lck_st_Client == x_ama ) {
      if( gio_Mbr_ama_Master == I_am_the ) {
         do {
            if((err=xdr_enc_uint32(enc, gulm_lock_login_rpl)) != 0) break;
            if((err=xdr_enc_uint32(enc, gio_Err_Ok)) != 0) break;
            if((err=xdr_enc_uint8(enc, I_am_the)) != 0) break;
            if((err=xdr_enc_flush(enc)) != 0) break;
         }while(0);
         if(err != 0 ) {
            log_msg(lgm_Network,
                  "Errors %d:%s trying to send login reply to fd:%d, %s\n",
                  err, strerror(errno), poller.polls[idx].fd, 
                  poller.ipn[idx].name);
            close_by_idx(idx);
            if( x_name != NULL ) {free(x_name); x_name = NULL;}
            return;
         }

         if( poller.ipn[idx].name != NULL ) free( poller.ipn[idx].name );
         poller.ipn[idx].name = x_name;
         poller.state[idx] = poll_Open;
         poller.type[idx]  = poll_Client;
         poller.times[idx] = 0;
         log_msg(lgm_Network,"New Client: idx %d fd %d from %s\n",
               idx, poller.polls[idx].fd,
               print_ipname(&poller.ipn[idx]));

         /* play back any pending drop requests. */
         playback_droprequests(idx, x_name);

         return;
      }else{
         do {
            if(xdr_enc_uint32(enc, gulm_lock_login_rpl) != 0) break;
            if(xdr_enc_uint32(enc, gio_Err_NotAllowed) != 0) break;
            if(xdr_enc_uint8(enc, I_am_the) != 0) break;
            if(xdr_enc_flush(enc) != 0) break;
         }while(0);
         close_by_idx(idx);
      }
   }else
   {
      do {
         if(xdr_enc_uint32(enc, gulm_lock_login_rpl) != 0) break;
         if(xdr_enc_uint32(enc, gio_Err_BadLogin) != 0) break;
         if(xdr_enc_uint8(enc, I_am_the) != 0) break;
         if(xdr_enc_flush(enc) != 0) break;
      }while(0);
      close_by_idx(idx);
   }
   free(x_name); x_name = NULL;
}

/**
 * recv_some_data - 
 * @idx: 
 * 
 * 
 */
static void recv_some_data(int idx)
{
   xdr_dec_t *dec = poller.dec[idx];
   xdr_enc_t *enc = poller.enc[idx];
   uint32_t code=0;
   uint8_t *x_name = NULL;
   int err;
   Waiters_t *lkrq;

   if( dec == NULL ) {
      log_err("There is no Decoder on poller (%s idx:%d fd:%d)!!\n",
            print_ipname(&poller.ipn[idx]),
            idx, poller.polls[idx].fd);
      return;
   }
   if( enc == NULL ) {
      log_err("There is no Encoder on poller (%s idx:%d fd:%d)!!\n",
            print_ipname(&poller.ipn[idx]),
            idx, poller.polls[idx].fd);
      return;
   }

   errno=0;
   err = xdr_dec_uint32(dec, &code);
   if( err == -EPROTO ) {
      log_msg(lgm_Network, "EOF on xdr (%s idx:%d fd:%d)\n",
            print_ipname(&poller.ipn[idx]),
            idx, poller.polls[idx].fd);
      close_by_idx(idx);
      return;
   }
   if( err != 0 ) {
      log_msg(lgm_Always, "Error on xdr (%s): %d:%d:%s\n", 
            print_ipname(&poller.ipn[idx]),
            err, errno, strerror(errno));
      /* err == -EPROTO  from xdr_* means it read EOF.
       */
      close_by_idx(idx);
      return;
   }

   if( gulm_lock_login_req == code ) {
      do_login(idx);
   }else
   if( gulm_lock_logout_req == code ) {
      /* gets closed right away, so we can ignore errors since that is
       * exactly what we would do if we saw one.
       */
      xdr_enc_uint32(enc, gulm_lock_logout_rpl);
      xdr_enc_flush(enc);
      close_by_idx(idx);
   }else
   if( code == gulm_socket_close ) {
      close_by_idx(idx);
   }else
   if( gulm_core_mbr_updt == code ) {
      struct in6_addr x_ip;
      uint8_t x_cur_state=-1;
      do {
         if((err=xdr_dec_string(dec, &x_name)) != 0) break;
         if((err=xdr_dec_ipv6(dec, &x_ip)) != 0 ) break;
         if((err=xdr_dec_uint8(dec, &x_cur_state)) != 0) break;
      }while(0);
      if( err != 0 ) {
         if(x_name!=NULL) {free(x_name); x_name = NULL;}
         return;
      }

      log_msg(lgm_Subscribers, "Recvd mbrupdt: %s, %s:%#x\n",
            x_name, gio_mbrupdate_to_str(x_cur_state), x_cur_state);

      /* save it */
      update_nodel(nodelists, x_name, &x_ip, x_cur_state);

      if( I_am_the == gio_Mbr_ama_Slave ) {
         if( x_cur_state == gio_Mbr_Expired ||
             x_cur_state == gio_Mbr_Logged_out ) {
            if( MasterIN.name != NULL ) {
               if(IN6_ARE_ADDR_EQUAL(x_ip.s6_addr32, MasterIN.ip.s6_addr32) ||
                  strcmp(x_name, MasterIN.name) == 0 ) {
                  /* Master Died! */
                  I_am_the = gio_Mbr_ama_Pending;
                  close_by_idx(poller.MasterIDX);
               }
            }
            if( strcmp(myName, x_name) == 0 ) {
               log_msg(lgm_Network, "Core is shutting down.\n");
               /* or should this get done differently? */
               running = FALSE;
            }
         }
      }else
      if( I_am_the == gio_Mbr_ama_Master ||
          I_am_the == gio_Mbr_ama_Arbitrating ) {
         if( x_cur_state == gio_Mbr_Logged_out ) {
            int t;
            if( (t=get_slave_idx(x_name)) != -1 ) {
               remove_slave_from_list_by_name(x_name);
               close_by_idx(t);
            }
            if( strcmp(myName, x_name) == 0 ) {
               log_msg(lgm_Network, "Core is shutting down.\n");
               /* or should this get done differently? */
               running = FALSE;
            }
         }
      }
      /* this is done no matter if it was kernel or userspace. */
      if( x_cur_state == gio_Mbr_Expired ) {
         expire_locks(x_name);
         expire_queued_dropreqs(x_name);
         remove_slave_from_list_by_name(x_name);
         /* when expired, *everything* need to be closed out. */
         close_all_named(x_name);
      }
      free(x_name); x_name = NULL;
   }else
   if( gulm_core_mbr_lstrpl == code ) {
      uint64_t x_x;
      uint32_t x_y;
      struct in6_addr x_ip;
      uint8_t x_st, x_m;
      do {
         if((err=xdr_dec_list_start(dec)) != 0 ) break;
      }while(0);
      if( err != 0 ) {
         return;
      }
      while( xdr_dec_list_stop(dec) != 0) {
         do {
            if((err=xdr_dec_string(dec, &x_name)) != 0) break;
            if((err=xdr_dec_ipv6(dec, &x_ip)) != 0 ) break;
            if((err=xdr_dec_uint8(dec, &x_st)) != 0) break;
            if((err=xdr_dec_uint8(dec, &x_m)) != 0) break;
            if((err=xdr_dec_uint8(dec, &x_m)) != 0) break;
            if((err=xdr_dec_uint32(dec, &x_y)) != 0 ) break;
            if((err=xdr_dec_uint64(dec, &x_x)) != 0 ) break;
            if((err=xdr_dec_uint64(dec, &x_x)) != 0 ) break;
            if((err=xdr_dec_uint64(dec, &x_x)) != 0 ) break;
         }while(0);
         if( err != 0 ) {
            if(x_name!=NULL) {free(x_name); x_name = NULL;}
            return;
         }
         update_nodel(nodelists, x_name, &x_ip, x_st);
         if(x_name!=NULL) {free(x_name); x_name = NULL;}
      }
   }else
   if( gulm_core_state_chgs == code ) {
      uint8_t x_st, x_q;
      struct in6_addr x_ip;
      do {
         if((err=xdr_dec_uint8(dec, &x_st)) != 0 ) break;
         if((err=xdr_dec_uint8(dec, &x_q)) != 0 ) break;
         if( x_st == gio_Mbr_ama_Slave ) {
            if((err=xdr_dec_ipv6(dec, &x_ip)) != 0 ) break;
            if((err=xdr_dec_string(dec, &x_name)) != 0 ) break;
         }
      }while(0);
      if( err != 0 ) {
         log_err("Failed to recv Core state update! %s\n", strerror(errno));
      }else{
         /* 
          * This could use a little clean up.
          */
         if( x_st == gio_Mbr_ama_Slave ) {
            if( I_am_the == gio_Mbr_ama_Slave ){
               /* nothing to change.
                * Need to figure out why doubles come through.
                * */
            }else{

               /* if somehow still connected to a Master, drop it. */
               close_by_idx( poller.MasterIDX );

               /* copy in master info. */
               if( MasterIN.name != NULL ) {
                  free(MasterIN.name);
                  MasterIN.name = NULL;
               }
               MasterIN.name = strdup(x_name);
               if( MasterIN.name == NULL )
                  die(ExitGulm_NoMemory, "Out of Memory.\n");
               memcpy(&MasterIN.ip, &x_ip, sizeof(struct in6_addr));
               log_msg(lgm_Always, "New Master at %s\n",
                     print_ipname(&MasterIN));

               /* The if down in the lt_main_loop will detect that we need
                * to loginto the master, and does that for us now.
                */
            }
         }else
         if( x_st == gio_Mbr_ama_Master ) {

            if( I_am_the != gio_Mbr_ama_Master ) {
               if( MasterIN.name != NULL ) {
                  free(MasterIN.name);
                  MasterIN.name = NULL;
               }
               memset(&MasterIN.ip, 0, sizeof(struct in6_addr));
               /* close connection */
               close_by_idx( poller.MasterIDX );
            }
         }else
         if( x_st == gio_Mbr_ama_Pending ) {
            if( I_am_the != gio_Mbr_ama_Pending ) {
               /* if the new state is Pending, and we weren't Pending
                * before, we need to make sure that no clients are
                * connected.  Well behaved clients won't connect, but in the
                * future when I'm not gonna be writing all of the clients,
                * I cannot always rely on that.
                */
               close_all_clients();

               /* if we had been a slave, we don't want this info anymore.
                */
               if( MasterIN.name != NULL ) {
                  free(MasterIN.name);
                  MasterIN.name = NULL;
               }
               memset(&MasterIN.ip, 0, sizeof(struct in6_addr));
               /* close connection */
               close_by_idx( poller.MasterIDX );
            }
         }else
         if( x_st == gio_Mbr_ama_Arbitrating ) {
            if( I_am_the != gio_Mbr_ama_Arbitrating ) {
               /* nothing here. */
            }
         }else
         {
            log_msg(lgm_Always, "Wierd new server state %d\n", x_st);
         }

         I_am_the = x_st;
         log_msg(lgm_ServerState, "New State: %s\n", gio_I_am_to_str(x_st));
      }
      if( x_name != NULL ) {free(x_name); x_name = NULL;}
   }else
   if( gulm_info_stats_req == code ) {
      xdr_enc_uint32(enc, gulm_info_stats_rpl);
      xdr_enc_list_start(enc);
      send_io_stats(enc);
      send_stats(enc);
      xdr_enc_list_stop(enc);
      xdr_enc_flush(enc);
   }else
   if( gulm_info_set_verbosity == code ) {
      if( xdr_dec_string(dec, &x_name) == 0 ) {
      set_verbosity(x_name, &verbosity);
         if( x_name != NULL ) { free(x_name); x_name = NULL; }
      }
      close_by_idx(idx);
   }else
   if( gulm_info_slave_list_req == code ) {
      xdr_enc_uint32(enc, gulm_info_slave_list_rpl);
      dump_slave_list(enc);
      close_by_idx(idx);
   }else
   if( gulm_lock_dump_req == code ) {
      xdr_enc_uint32(enc, gulm_lock_dump_rpl);
      xdr_enc_flush(enc);
      serialize_lockspace( poller.polls[idx].fd );
   }else
   if( gulm_lock_rerunqueues == code ) {
      rerun_wait_queues();
      recheck_reply_waiters(Slave_bitmask, 0);
      close_by_idx(idx);
   }else
   if( gio_Mbr_ama_Slave == I_am_the ) {
      /*************************************************************/
      if( gulm_lock_state_updt == code ) {
         lkrq = get_new_lkrq();
         if( lkrq == NULL ) die(ExitGulm_NoMemory, "No memory.\n");

         if(pack_lkrq_from_io(lkrq, code, dec, idx) == 0 )
            err = force_lock_state(lkrq);
      }else
      if( gulm_lock_action_updt == code ) {
         lkrq = get_new_lkrq();
         if( lkrq == NULL ) die(ExitGulm_NoMemory, "No memory.\n");

         if(pack_lkrq_from_io(lkrq, code, dec, idx) == 0 )
            err = force_lock_action(lkrq);
      }else
      if( gulm_lock_drop_exp == code ) {
         uint8_t *x_mask = NULL;
         uint16_t x_len=0;
         do {
            if((err = xdr_dec_string(dec, &x_name)) != 0 ) break;
            if((err = xdr_dec_raw_m(dec, (void**)&x_mask, &x_len)) != 0 ) break;
         }while(0);
         if( err == 0 ) {
            drop_expired(x_name, x_mask, x_len);
            if( x_name == NULL ) {
               log_msg(lgm_locking,"Dropped expired locks for NULL\n");
            }else{
               log_msg(lgm_locking,"Dropped expired locks for %s\n", x_name);
            }
         }
         if(x_name != NULL ) {free(x_name); x_name = NULL;}
         if(x_mask != NULL ) {free(x_mask); x_mask = NULL;}
      }else
      {
         xdr_enc_uint32(enc, gulm_err_reply);
         xdr_enc_uint32(enc, code);
         xdr_enc_uint32(enc, gio_Err_NotAllowed);
         xdr_enc_flush(enc);
      }
   }else
   if( gio_Mbr_ama_Master == I_am_the ||
       gio_Mbr_ama_Arbitrating == I_am_the ) {
      /*************************************************************/
      if( gulm_lock_drop_exp == code ) {
         uint8_t *x_mask = NULL;
         uint16_t x_len=0;
         do {
            if((err = xdr_dec_string(dec, &x_name)) != 0 ) break;
            if((err = xdr_dec_raw_m(dec, (void**)&x_mask, &x_len)) != 0 ) break;
         }while(0);
         if( err == 0 ) {
            drop_expired(x_name, x_mask, x_len);
            if( x_name == NULL ) {
               log_msg(lgm_locking,"Dropped expired locks for NULL\n");
            }else{
               log_msg(lgm_locking,"Dropped expired locks for %s\n", x_name);
            }
            send_drop_exp_to_slaves(x_name, x_mask, x_len);
         }
         if(x_name != NULL ) {free(x_name); x_name = NULL;}
         if(x_mask != NULL ) {free(x_mask); x_mask = NULL;}
      }else
      if( gulm_lock_update_rpl == code ) {
         uint8_t *x_key=NULL;
         uint16_t x_len;
         int soff;
         if( (err = xdr_dec_raw_m(dec, (void**)&x_key, &x_len)) == 0 ) {

            /* find slave_index for this slave. */
            if( (soff = get_slave_offset(idx)) == -1 ) {
               /* ERROR! */
               log_err("CANNOT FIND SLAVE %s !!!!!\n", poller.ipn[idx].name);
            } else {
               log_msg(lgm_LockUpdates,
                     "Slave reply from %s so:%d for lock %s\n",
                     poller.ipn[idx].name, soff, lkeytob64(x_key, x_len));
               increment_slave_update_replies(x_key, x_len, soff,Slave_bitmask);
            }
         }else{
            log_err("xdr_dec error %d\n", err);
         }

         if( x_key != NULL ) {free(x_key); x_key = NULL;}
      }else
      if( (lkrq = get_new_lkrq()) == NULL ) {
         log_err("Out Of Memory!\n");
         xdr_enc_uint32(enc, gulm_err_reply);
         xdr_enc_uint32(enc, code);
         xdr_enc_uint32(enc, gio_Err_MemoryIssues);
         xdr_enc_flush(enc);
      }else
      if( pack_lkrq_from_io(lkrq, code, dec, idx) != 0) {
         log_err("Out Of Memory!\n");
         xdr_enc_uint32(enc, gulm_err_reply);
         xdr_enc_uint32(enc, code);
         xdr_enc_uint32(enc, gio_Err_MemoryIssues);
         xdr_enc_flush(enc);
      }else
      if( gulm_lock_state_req == code ) {
         err = do_lock_state(lkrq);
         /* the lkrq will be freed up after the slave update replies have
          * been recved.
          */
      }else
      if( gulm_lock_action_req == code ) {
         err = do_lock_action(lkrq);
         /* the lkrq will be freed up after the slave update replies have
          * been recved.
          */
      }else
      if( gulm_lock_query_req == code ) {
         err = do_lock_query(lkrq);
      }else
      {
         log_err("Unexpected op code %#x (%s), on fd:%d name:%s\n",
               code, gio_opcodes(code),
               poller.polls[idx].fd, poller.ipn[idx].name);

         close_by_idx(idx);
      }
   }else
   {
      log_err("Unexpected op code %#x (%s), on fd:%d name:%s\n",
            code, gio_opcodes(code),
            poller.polls[idx].fd, poller.ipn[idx].name);

      close_by_idx(idx);
   }

}

/**
 * get_core_state - 
 * 
 * Returns: int
 */
static int get_core_state(void)
{
   int err;
   xdr_enc_t *enc = poller.enc[poller.coreIDX];
   if((err=xdr_enc_uint32(enc, gulm_core_state_req))!=0) return err;
   if((err=xdr_enc_flush(enc))!=0) return err;

   /* grab nodelist too while we're at it. */
   if((err=xdr_enc_uint32(enc, gulm_core_mbr_lstreq))!=0) return err;
   if((err=xdr_enc_flush(enc))!=0) return err;
   return 0;
}

/**
 * lt_main_loop - 
 *
 * This loop handles incommings.
 * 
 * Returns: int
 */
void lt_main_loop(void)
{
   int cnt, idx;
   extern unsigned long cnt_replyq;

   init_lt_slave_list();
   if((nodelists = initialize_nodel())==NULL)
      die(ExitGulm_NoMemory, "Out of memory.\n");
   gettimeofday(&Started_at, NULL);
   gettimeofday(&NOW, NULL);
   get_core_state();

   while( running ) {

      /* We're supposed to be connected to a Master server, but we seem not
       * to be at the moment.  So try again.
       * Actually, if this works out, I may leave this to be the perferred
       * method of logging into the server.
       * it does.
       */
      if( I_am_the == gio_Mbr_ama_Slave &&
          MasterIN.name != NULL &&
          !IN6_IS_ADDR_UNSPECIFIED(MasterIN.ip.s6_addr32) &&
          poller.MasterIDX == -1 &&
          logging_into_master == FALSE )
         logintoMaster();

      if( (cnt = poll(poller.polls, poller.maxi +1, 1000)) <= 0) {
         if( cnt < 0 && errno != EINTR )
            log_err("poll error: %s\n",strerror(errno));
         if(!running) return;
      }
      gettimeofday(&NOW, NULL);

      /* for shits and giggles.
       *  if reply_waiters > 3000 skip clients with data.
       * should protect against spikes pretty well.
       * sustained load will kill.
       */

      for( idx=0; idx <= poller.maxi ; idx++) {
         if( poller.polls[idx].fd < 0) continue;
         if( poller.polls[idx].revents & POLLHUP ) {
            remove_slave_from_list(idx);
            close_by_idx(idx);
         }
         if (poller.polls[idx].revents & POLLNVAL ) {
            remove_slave_from_list(idx);
            close_by_idx(idx);
         }
         if( poller.polls[idx].revents & POLLOUT ) {
            send_some_data(idx);
         }
         if( poller.polls[idx].revents & POLLIN ) {
            poller.polls[idx].revents &= ~POLLIN; /*clear in case of swap*/
            if( poller.polls[idx].fd == poller.listenFD ) {
               accept_connection();
            }else
            {
               if( poller.state[idx] == poll_Trying ) {
                  /* we're trying to loginto the master and become a slave. */
                  if( recv_Masterlogin_reply(idx) != 0 )
                     close_by_idx(idx);
                  /* should retry the login too. */
               }else{
                  if( poller.type[idx] != poll_Client ||
                      cnt_replyq < 3000 )
                     recv_some_data(idx);
               }
            }
         }
         /* check for timed out pollers. */
         if( poller.times[idx] != 0 &&
             poller.times[idx]+ gulm_config.new_con_timeout < tvs2uint64(NOW)) {
            log_msg(lgm_Network, "Timeout (%"PRIu64") on idx: %d fd:%d "
                  "(%s)\n",
                  gulm_config.new_con_timeout, idx, poller.polls[idx].fd, 
                  print_ipname(&poller.ipn[idx]));
            if( poller.state[idx] == poll_Trying ) logging_into_master = FALSE;
            remove_slave_from_list(idx);
            close_by_idx(idx);
         }
         if(!running) return;
      }/*for( i=0; i <= poller.maxi ; i++)*/

   }/* while(running) */
}


/* vim: set ai cin et sw=3 ts=3 : */
