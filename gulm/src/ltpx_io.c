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
#include "ltpx_priv.h"
#include "config_gulm.h"
#include "utils_crc.h"
#include "utils_ip.h"
#include "utils_tostr.h"
#include "utils_verb_flags.h"
#include "config_ccs.h"


/*****************************************************************************/
/* bits of data used by the log_*() and die() functions. */
extern uint32_t verbosity;
extern char *ProgramName;

/* signal checks. */
extern int SIGTERM_TRIPPED;

/* stuff from config. */
extern gulm_config_t gulm_config;
extern struct in6_addr myIP;
extern char myName[256];

/*****************************************************************************/
/*****************************************************************************/

static struct timeval Started_at;
static struct timeval NOW;

static ip_name_t MasterIN = {{NULL,NULL,NULL},IN6ADDR_ANY_INIT,NULL};
static int I_am_the = gio_Mbr_ama_Pending;/* what state we are in */

typedef struct lsfilter_s {
   uint8_t *key;
   uint16_t len;
} ls_filter_t;

typedef enum {poll_Closed, poll_Accepting, poll_Trying, poll_Open} poll_state;
typedef enum {poll_Unknown, poll_Client, poll_Master} poll_type;
struct {
   struct pollfd *polls;
   xdr_enc_t **enc;
   xdr_dec_t **dec;
   poll_state *state;
   poll_type *type;
   uint64_t *times;
   ip_name_t *ipn;
   ls_filter_t *space;
   unsigned int maxi;
   int coreIDX;
   int listenFD; /* socket for new connections. */
} poller;

typedef struct {
   int poll_idx;
   int logging_in;
   uint8_t start;
   uint8_t stop;
   hash_t *pending_reqs;
   Qu_t senderlist;
   uint32_t senderlistlen;
   uint32_t pendreqcnt;
   uint64_t lastsentat;
} Masters_t;
Masters_t MastersList[256];
/* index in array == lt_id */

char *lkeytohex(uint8_t *key, uint8_t keylen);
#if 0
static void print_master_entry(int ltid)
{
   log_msg(lgm_Always, "poll_idx     = %d\n", MastersList[ltid].poll_idx);
   log_msg(lgm_Always, "logging_in   = %d\n", MastersList[ltid].logging_in);
   log_msg(lgm_Always, "start        = %d\n", MastersList[ltid].start);
   log_msg(lgm_Always, "stop         = %d\n", MastersList[ltid].stop);
   log_msg(lgm_Always, "pending_reqs = %p\n", MastersList[ltid].pending_reqs);
}
/**
 * print_poller_entry - 
 * @idx: 
 * 
 * 
 */
static void print_poller_entry(int idx)
{
   char *s;
#define CasedString(x)  case (x):  s = #x ; break;
   log_msg(lgm_Always, "poller idx         = %d\n", idx);
   log_msg(lgm_Always, "polls[].fd         = %d\n", poller.polls[idx].fd);
   log_msg(lgm_Always, "polls[].events     = %x\n", poller.polls[idx].events);
   log_msg(lgm_Always, "polls[].revents    = %x\n", poller.polls[idx].revents);
   switch(poller.state[idx]){
      CasedString(poll_Closed);
      CasedString(poll_Accepting);
      CasedString(poll_Trying);
      CasedString(poll_Open);
   }
   log_msg(lgm_Always, "state[]            = %s\n", s);
   switch(poller.type[idx]){
      CasedString(poll_Unknown);
      CasedString(poll_Client);
      CasedString(poll_Master);
   }
   log_msg(lgm_Always, "type[]             = %s\n", s);
   log_msg(lgm_Always, "times[]            = %"PRId64"\n", poller.times[idx]);
   log_msg(lgm_Always, "ipn[].name         = %s\n", poller.ipn[idx].name);
   log_msg(lgm_Always, "ipn[].ip           = %s\n",iptostr(poller.ipn[idx].ip));
   log_msg(lgm_Always, "enc[]              = %p\n", poller.enc[idx]);
   log_msg(lgm_Always, "dec[]              = %p\n", poller.dec[idx]);
#undef CasedString
}
#endif

/**
 * initialize_MastersList - 
 * 
 */
void initialize_MastersList(void)
{
   int i;
   for(i=0; i < 256; i++ ) {
      MastersList[i].poll_idx = -1;
      MastersList[i].logging_in = FALSE;
      MastersList[i].pending_reqs = NULL;
      MastersList[i].start = 0;
      MastersList[i].stop = 0;
      MastersList[i].senderlistlen = 0;
      MastersList[i].pendreqcnt = 0;
      Qu_init_head( &MastersList[i].senderlist );
   }
   for(i=0; i < gulm_config.how_many_lts; i ++ ) {
      int a,b;
      get_lt_range(i, gulm_config.how_many_lts, &a, &b);
      MastersList[i].start = a;
      MastersList[i].stop = b;
      MastersList[i].pending_reqs = create_new_req_map();
   }
}

/**
 * init_ltpx_poller - 
 * 
 * Returns: int
 */
int init_ltpx_poller(void)
{
   int i;

   memset(&poller, 0, sizeof(poller));

   poller.polls = malloc(open_max() * sizeof(struct pollfd));
   if( poller.polls == NULL ) goto nomem;
   memset(poller.polls, 0, (open_max() * sizeof(struct pollfd)));

   poller.type = malloc(open_max() * sizeof(poll_type));
   if( poller.type == NULL ) goto nomem;

   poller.state = malloc(open_max() * sizeof(poll_state));
   if( poller.state == NULL ) goto nomem;

   poller.times = malloc(open_max() * sizeof(uint64_t));
   if( poller.times == NULL ) goto nomem;

   poller.ipn = malloc(open_max() * sizeof(ip_name_t));
   if( poller.ipn == NULL ) goto nomem;

   poller.space = malloc(open_max() * sizeof(ls_filter_t));
   if( poller.space == NULL ) goto nomem;

   poller.enc = malloc(open_max() * sizeof(xdr_enc_t*));
   if( poller.enc == NULL ) goto nomem;

   poller.dec = malloc(open_max() * sizeof(xdr_dec_t*));
   if( poller.dec == NULL ) goto nomem;

   for(i=0; i < open_max(); i++) {
      poller.polls[i].fd = -1;
      poller.state[i] = poll_Closed;
      poller.type[i] = poll_Unknown;
      poller.times[i] = 0;
      memset(&poller.ipn[i].ip, 0, sizeof(struct in6_addr));
      poller.ipn[i].name = NULL;
      poller.enc[i] = NULL;
      poller.dec[i] = NULL;
   }

   poller.maxi = 0;
   poller.coreIDX = -1;
   poller.listenFD = -1;

   return 0;
nomem:
   if(poller.polls) free(poller.polls);
   if(poller.state) free(poller.state);
   if(poller.type) free(poller.type);
   if(poller.times) free(poller.times);
   if(poller.space) free(poller.space);
   if(poller.ipn) free(poller.ipn);
   if(poller.enc) free(poller.enc);
   if(poller.dec) free(poller.dec);
   return -ENOMEM;
}

void release_ltpx_poller(void)
{
   if(poller.polls) free(poller.polls);
   if(poller.state) free(poller.state);
   if(poller.type) free(poller.type);
   if(poller.times) free(poller.times);
   if(poller.space) free(poller.space);
   if(poller.ipn) free(poller.ipn);
   if(poller.enc) free(poller.enc);
   if(poller.dec) free(poller.dec);
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
   poller.type[i] = poll_Unknown;
   poller.times[i] = t;
   memcpy(&poller.ipn[i].ip, ip, sizeof(struct in6_addr));
   if( name != NULL ) poller.ipn[i].name = strdup(name);
   else poller.ipn[i].name = NULL;
   poller.space[i].key = NULL; /* space of NULL is same as all */
   poller.space[i].len = 0;
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
 * get_ltid_from_poller_idx - 
 * @idx: 
 * 
 * 
 * Returns: int
 */
static int get_ltid_from_poller_idx(int idx)
{
   int lt_id;
   for(lt_id = 0; lt_id < 256; lt_id ++) {
      if( MastersList[lt_id].poll_idx == idx ) {
         return lt_id;
      }
   }
   return -1;
}

int reset_master_login(int poll_idx)
{
   int lt_id;
   if( (lt_id = get_ltid_from_poller_idx(poll_idx)) < 0 ) return -1;
   MastersList[lt_id].poll_idx = -1;
   MastersList[lt_id].logging_in = FALSE;
   return 0;
}

static void __inline__ close_by_idx(int idx)
{
   if( idx < 0 || idx > open_max() ) return;
   log_msg(lgm_Network2, "Closing connection idx:%d, fd:%d to %s\n",
         idx, poller.polls[idx].fd, poller.ipn[idx].name);
   /* If we just closed the connect to the Master, set things up to try to
    * re-find it.
    * gotta do this before I wipe all the info out.
    */
   if( poller.type[idx] == poll_Master ) {
      int m;
      m = reset_master_login(idx);
      log_msg(lgm_Network2, "Connection to Master %d closed.\n", m);
   }

    if( poller.coreIDX == idx ) 
      die(ExitGulm_Assertion, "Lost connection to core, cannot continue. "
           "node reset required to re-activate cluster operations.\n" );
   GULMD_ASSERT( poller.polls[idx].fd != poller.listenFD , );

   close( poller.polls[idx].fd );
   poller.polls[idx].fd = -1;
   poller.polls[idx].revents = 0; /* clear any other events. */
   poller.state[idx] = poll_Closed;
   poller.type[idx] = poll_Unknown;
   poller.times[idx] = 0;
   memset(&poller.ipn[idx].ip, 0, sizeof(struct in6_addr));
   if( poller.ipn[idx].name != NULL ) {
      free(poller.ipn[idx].name);
      poller.ipn[idx].name = NULL;
   }
   if( poller.space[idx].key != NULL ) {
      free( poller.space[idx].key );
      poller.space[idx].key = NULL;
   }
   poller.space[idx].len = 0;
   if( poller.enc[idx] != NULL ) {
      xdr_enc_release(poller.enc[idx]);
      poller.enc[idx] = NULL;
   }
   if( poller.dec[idx] != NULL ) {
      xdr_dec_release(poller.dec[idx]);
      poller.dec[idx] = NULL;
   }
}

void close_all_masters(void)
{
   int ltid;
   log_msg(lgm_Network2, "Closing all Master connections\n");
   for(ltid=0; ltid < 256; ltid ++ ) {
      if( MastersList[ltid].poll_idx >= 0 ) {
         log_msg(lgm_Network2, "Closing Master ltid:%d pollidx:%d lgin:%d\n",
               ltid, MastersList[ltid].poll_idx, MastersList[ltid].logging_in);
         /* so close_by_idx() doesn't call reset_master_login(); */
         poller.type[MastersList[ltid].poll_idx] = poll_Unknown;
         /* close it */
         close_by_idx(MastersList[ltid].poll_idx);
         MastersList[ltid].poll_idx = -1;
         MastersList[ltid].logging_in = FALSE;
      }
   }
}

/**
 * dump_all_master_tables - 
 * @oid: 
 * 
 * 
 * Returns: void
 */
void dump_all_master_tables(void)
{
   int ltid;
   for(ltid=0; ltid < gulm_config.how_many_lts; ltid ++ ) {
      dump_ltpx_senders(&MastersList[ltid].senderlist, ltid);
      if( MastersList[ltid].pending_reqs != NULL ) {
         dump_ltpx_locks(MastersList[ltid].pending_reqs, ltid);
      }
   }
}

int open_ltpx_listener(int port)
{
   poller.listenFD = serv_listen(port);
   if( poller.listenFD < 0 ) return -1;
   add_to_pollers(poller.listenFD, poll_Open, 0, "_ listener _", &in6addr_any);
   /* no xdr on the listener socket. */
   return 0;
}

/**
 * open_jid_to_core - 
 * 
 * 
 * Returns: int
 */
int open_ltpx_to_core(void)
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
      if((err = xdr_enc_string(poller.enc[idx], "LTPX"))<0) break;
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

/*****************************************************************************/
static int send_io_stats(xdr_enc_t *enc)
{
   struct timeval tv;
   int ltid;
   char tmp[256] = "4: There is no goat.";

   xdr_enc_string(enc, "I_am");
   xdr_enc_string(enc, gio_I_am_to_str(I_am_the));

   if( MasterIN.name != NULL ) {
      xdr_enc_string(enc, "Master");
      xdr_enc_string(enc, MasterIN.name);
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

   for(ltid=0; ltid < gulm_config.how_many_lts; ltid ++ ) {
      snprintf(tmp, 256, "Master.%d.senderlistlen", ltid);
      xdr_enc_string(enc, tmp);
      snprintf(tmp, 256, "%u", MastersList[ltid].senderlistlen);
      xdr_enc_string(enc, tmp);

      snprintf(tmp, 256, "Master.%d.pendreqcnt", ltid);
      xdr_enc_string(enc, tmp);
      snprintf(tmp, 256, "%u", MastersList[ltid].pendreqcnt);
      xdr_enc_string(enc, tmp);
   }

   return 0;
}

/*****************************************************************************/
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

   log_msg(lgm_Network2, "Accepted Connection idx:%d, fd:%d to %s\n",
         i, poller.polls[i].fd, poller.ipn[i].name);
   return 0;
}

/*****************************************************************************/
/**
 * send_lk_st_req - 
 * @ltid: 
 * @lq: 
 * 
 * FIXME Things can get stuck here if this was writing to the master when
 * it died.  Evilly is that even though core is quick about figuring and
 * broadcasting that out, we're stuck in our single thread behind a write.
 * 
 * Returns: int
 */
int send_lk_st_req(int ltid, lock_req_t *lq)
{
   int err=0;
   xdr_enc_t *enc;
   if(ltid > 255 || ltid < 0 ) {
      return -EINVAL;
   }
   if( MastersList[ltid].poll_idx < 0 ||
       MastersList[ltid].poll_idx > open_max() ) {
      /* master has gone away, don't try to send to them.  This will end up
       * just queuing the req for later.
       */
      return -EINVAL;
   }
   enc = poller.enc[MastersList[ltid].poll_idx];

   do{
      if((err = xdr_enc_uint32(enc, gulm_lock_state_req)) != 0 ) break;
      if((err = xdr_enc_raw(enc, lq->key, lq->keylen)) != 0 ) break;
      if((err = xdr_enc_uint8(enc, lq->state)) != 0 ) break;
      if((err = xdr_enc_uint32(enc, lq->flags)) != 0 ) break;
      if( lq->flags & gio_lck_fg_hasLVB )
         if((err = xdr_enc_raw(enc, lq->lvb, lq->lvblen)) != 0 ) break;
      if((err = xdr_enc_flush(enc)) != 0 ) break;
   }while(0);
   if( err != 0 ) {
      log_err("XDR error %d:%s sending to lt%03d\n",
            err, strerror(abs(err)), ltid);
   }
   return err;
}

/**
 * send_lk_act_req - 
 * @ltid: 
 * @lq: 
 * 
 * 
 * Returns: int
 */
int send_lk_act_req(int ltid, lock_req_t *lq)
{
   int err=0;
   xdr_enc_t *enc;
   if(ltid > 255 || ltid < 0 ) {
      return -EINVAL;
   }
   if( MastersList[ltid].poll_idx < 0 ||
       MastersList[ltid].poll_idx > open_max() ) {
      /* master has gone away, don't try to send to them.  This will end up
       * just queuing the req for later.
       */
      return -EINVAL;
   }
   enc = poller.enc[MastersList[ltid].poll_idx];

   do{
      if((err = xdr_enc_uint32(enc, gulm_lock_action_req)) != 0 ) break;
      if((err = xdr_enc_raw(enc, lq->key, lq->keylen)) != 0 ) break;
      if((err = xdr_enc_uint8(enc, lq->state)) != 0 ) break;
      if( lq->state == gio_lck_st_SyncLVB )
         if((err = xdr_enc_raw(enc, lq->lvb, lq->lvblen)) != 0 ) break;
      if((err = xdr_enc_flush(enc)) != 0 ) break;
   }while(0);
   if( err != 0 ) {
      log_err("XDR error %d:%s sending to lt%03d\n",
            err, strerror(err), ltid);
   }
   return err;
}

/**
 * send_lk_drop_req - 
 * @ltid: 
 * @lq: 
 * 
 * 
 * Returns: int
 */
int send_lk_drop_req(int ltid, lock_req_t *lq)
{
   int err=0;
   xdr_enc_t *enc;
   if(ltid > 255 || ltid < 0 ) {
      return -EINVAL;
   }
   if( MastersList[ltid].poll_idx < 0 ||
       MastersList[ltid].poll_idx > open_max() ) {
      /* master has gone away, don't try to send to them.  This will end up
       * just queuing the req for later.
       */
      return -EINVAL;
   }
   enc = poller.enc[MastersList[ltid].poll_idx];

   do{
      if((err = xdr_enc_uint32(enc, gulm_lock_drop_exp)) != 0 ) break;
      if((err = xdr_enc_string(enc, lq->lvb)) != 0 ) break;
      if((err = xdr_enc_raw(enc, lq->key, lq->keylen)) != 0 ) break;
      if((err = xdr_enc_flush(enc)) != 0 ) break;
   }while(0);
   if( err != 0 ) {
      log_err("XDR error %d:%s sending to lt%03d\n",
            err, strerror(err), ltid);
   }
   return err;
}

/*****************************************************************************/

/**
 * find_in_senders_list - 
 * @ltid: 
 * @key: 
 * @keylen: 
 * 
 * gross, but I don't know where else to look.
 * 
 * Returns: int
 */
int find_in_senders_list(int ltid, uint8_t *key, uint16_t keylen)
{
   lock_req_t *lq;
   LLi_t *q;

   for( q = LLi_next(&MastersList[ltid].senderlist);
        NULL != LLi_data(q);
        q = LLi_next(q) )
   {
      lq = LLi_data(q);
      if( memcmp(key, lq->key, MIN(keylen, lq->keylen)) == 0 ) {
         log_msg(lgm_Always, "XXX Found in senders %s\n",
               lkeytohex(key, keylen));
         return TRUE;
      }
   }
   return FALSE;
}

/**
 * resend_reqs - 
 * @item: 
 * @misc: 
 *
 * 
 * Returns: int
 */
int resend_reqs(LLi_t *item, void *misc)
{
   lock_req_t *lq = LLi_data(item);
   int ltid = *((int*)misc);

   find_in_senders_list(ltid, lq->key, lq->keylen);
   /* move to sender queue */
   LLi_del(item);
   LLi_unhook(item);
   Qu_EnQu_Front(&MastersList[ltid].senderlist, item);
   poller.polls[MastersList[ltid].poll_idx].events |= POLLOUT;
   MastersList[ltid].senderlistlen ++;
   MastersList[ltid].pendreqcnt --;

   return 0;
}

/*****************************************************************************/
/**
 * logintoMaster - 
 *
 * Returns: int
 */
static int logintoMaster(int lt_id)
{
   struct sockaddr_in6 adr;
   int idx,cmFD,err;
   xdr_enc_t *xdr;

   /* socket connect to CM */
   if((cmFD = socket(AF_INET6, SOCK_STREAM, 0)) <0){
      log_err("Failed to create socket. %s\n", strerror(errno));
      return -1;
   }

   adr.sin6_family = AF_INET6;
   memcpy(&adr.sin6_addr, &MasterIN.ip, sizeof(struct in6_addr));
   adr.sin6_port = htons(gulm_config.lt_port + lt_id);

   if( connect(cmFD, (struct sockaddr*)&adr, sizeof(struct sockaddr_in6))<0) {
      close(cmFD);
      log_msg(lgm_LoginLoops, "Cannot connect %s (%s)\n",
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
   idx = add_to_pollers(cmFD, poll_Trying, tvs2uint64(NOW),
                      MasterIN.name, &MasterIN.ip);

   MastersList[lt_id].poll_idx = idx;

   if( idx < 0 ) { /* out of free FDs. */
      log_err("Failed to find unused poller space.\n");
      close_by_idx(idx);
      return -1;
   }
   poller.type[idx] = poll_Master;
   if( add_xdr_to_poller(idx) < 0 ) {
      log_err("Failed to allocate memory for xdr.\n");
      close_by_idx(idx);
      return -1;
   }

   /* send login request */
   xdr = poller.enc[idx];

   do {
      if((err = xdr_enc_uint32(xdr, gulm_lock_login_req)) != 0) break;
      if((err = xdr_enc_uint32(xdr, GIO_WIREPROT_VERS)) != 0) break;
      if((err = xdr_enc_string(xdr, myName)) != 0) break;
      if((err = xdr_enc_uint8(xdr, gio_lck_st_Client)) != 0) break;
      if((err = xdr_enc_flush(xdr)) != 0) break;
   }while(0);
   if( err != 0 ) {
      log_msg(lgm_LoginLoops, "Errors trying to send login request. %d:%s\n",
            err, strerror(errno));
      close_by_idx(idx);
      return -1;
   }

   MastersList[lt_id].logging_in = TRUE;

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
   int err, ltid=-1;
   uint32_t code=0;
   uint32_t rpl_err=1;
   uint8_t rpl_ama=0;
   xdr_dec_t *dec = poller.dec[idx];

   /* recv login reply */
   do {
      if((err = xdr_dec_uint32(dec, &code)) != 0) break;
      if( code != gulm_lock_login_rpl ) {err=-1; errno=EPROTO; break;}
      if((err = xdr_dec_uint32(dec, &rpl_err)) != 0) break;
      if((err = xdr_dec_uint8(dec, &rpl_ama)) != 0) break;
   } while(0);
   if( err != 0 ) {
      log_err("Errors trying to login to LockTable Master: "
              "(%s idx:%d fd:%d) %d:%s\n",
            print_ipname(&poller.ipn[idx]),
            idx, poller.polls[idx].fd,
            err, strerror(errno));
      goto exit;
   }

   if( (ltid = get_ltid_from_poller_idx(idx)) < 0 ) {
      log_err("Got reply from a LockTable Master that we cannot match an "
              "ltid too. (%s idx:%d fd:%d)\n",
            print_ipname(&poller.ipn[idx]),
            idx, poller.polls[idx].fd);
      err = -EAGAIN;
      goto exit;
   }

   if( rpl_err != 0 ) {
      log_msg(lgm_Always,
            "Errors trying to login to LT%03d: (%s) %d:%s\n",
            ltid,
            print_ipname(&poller.ipn[idx]),
            rpl_err, gio_Err_to_str(rpl_err));
      err = rpl_err;
      goto exit;
   }

   if( rpl_ama != gio_Mbr_ama_Master ) {
      log_msg(lgm_Always, "Node %s is not a LockTable Master server.\n",
            print_ipname(&poller.ipn[idx]));
      err = -EAGAIN;
      goto exit;
   }

   poller.state[idx] = poll_Open;
   poller.times[idx] = 0;
   log_msg(lgm_Network, "Logged into LT%03d at %s\n", ltid,
         print_ipname(&MasterIN));

   if( MastersList[ltid].pending_reqs == NULL ) {
      log_err("Totally not good, we'll attempt to savlage"
            "But this ship is probably leaking.\n");
      MastersList[ltid].pending_reqs = create_new_req_map();
   }else
   {
      err = hash_walk(MastersList[ltid].pending_reqs, resend_reqs, &ltid);
      if( err != 0 ) {
         log_err("%d trying to resend requsts to LT%03d\n", err, ltid);
      }
      poller.polls[MastersList[ltid].poll_idx].events |= POLLOUT;

   }

   log_msg(lgm_Network, "Finished resending to LT%03d\n", ltid);

exit:
   if(ltid >= 0) MastersList[ltid].logging_in = FALSE;
   return err;
}

/**
 * checkup_masters - 
 *
 * Restart the log in if it isn't there for one of the masters.
 */
void checkup_masters(void)
{
   int ltid;

   for(ltid=0; ltid < gulm_config.how_many_lts; ltid++) {
      if( MastersList[ltid].poll_idx < 0 &&
          MastersList[ltid].logging_in == FALSE ) {
         logintoMaster(ltid);
      }
   }
}


/**
 * logout_of_Masters - 
 */
void logout_of_Masters(void)
{
   int ltid, err;
   xdr_enc_t *xdr;

   for(ltid=0; ltid < gulm_config.how_many_lts; ltid++) {
      if( MastersList[ltid].poll_idx != -1 ) {
         xdr = poller.enc[MastersList[ltid].poll_idx];

         do {
            if((err = xdr_enc_uint32(xdr, gulm_lock_logout_req)) != 0) break;
            if((err = xdr_enc_flush(xdr)) != 0) break;
         }while(0);
         if(err != 0 ) {
            log_err("couldn't send logout request. %d:%s\n",
                  err, strerror(err));
         }
      }
   }
}

/*****************************************************************************/
/**
 * send_senderlist - 
 * @ltid: 
 * 
 * Returns: int
 */
int send_senderlist(int ltid)
{
   int err = 0;
   lock_req_t *lq;
   Qu_t *q;

   /* only run one item at a time. 
    * need to get back to poll().
    * */
   if( !Qu_empty( &MastersList[ltid].senderlist ) ) {
      /* send next. */
      q = Qu_DeQu(&MastersList[ltid].senderlist);
      MastersList[ltid].senderlistlen --;
      LLi_unhook(q);
      lq = Qu_data(q);
      switch(lq->code) {
         case gulm_lock_state_req:
            err = send_lk_st_req(ltid, lq);
            break;
         case gulm_lock_action_req:
            err = send_lk_act_req(ltid, lq);
            break;
         case gulm_lock_drop_exp:
            err = send_lk_drop_req(ltid, lq);
            break;
         default:
            log_err("Unexpected opcode (%x:%s) on lock %s\n",
                  lq->code, gio_opcodes(lq->code),
                  lkeytohex(lq->key, lq->keylen));
            break;
      }
      if( err != 0 ) {
         log_err("Got a %d:%s trying to send packet to Master %d on %s\n",
               err, strerror(abs(err)), ltid,
               lkeytohex(lq->key, lq->keylen));
         /* stick it back on the queue. else we loose it. */
         Qu_EnQu_Front(&MastersList[ltid].senderlist, q);
         MastersList[ltid].senderlistlen ++;
         goto exit;
      }

      if( lq->code == gulm_lock_drop_exp ) {
         /* no replies for drop exp requests. */
         recycle_lock_req(lq);
      }else
      {
         err = hash_add( MastersList[ltid].pending_reqs, &lq->ls_list);
         if( err != 0 ) {
            log_err("AH! Postponed Dup Entries! Horror! Horror!\n");
         }
         MastersList[ltid].pendreqcnt ++;
      }
   }

   if( Qu_empty( &MastersList[ltid].senderlist ) ) 
      poller.polls[MastersList[ltid].poll_idx].events &= ~POLLOUT;

exit:
   return err;
}

/*****************************************************************************/

/* fixed 12byte-to-1byte hash.
 * given everything is fixed lengths, I should be able to make a nice fast
 * one. (/should/...)
 *
 * I hope this works well enough..... This should show how little I know
 * about hash functions.... (tests show it to seemingly work well enough.)
 */
uint8_t __inline__ fourtoone(uint32_t bighash)
{
   uint8_t hash;
   hash = (bighash >> 0);
   hash ^= (bighash >> 8);
   hash ^= (bighash >> 16);
   hash ^= (bighash >> 24);
   return hash;
}

/**
 * select_master_server - 
 * @key: 
 * @keylen: 
 * 
 * returns ltid of the master this key goes to.
 * 
 * Returns: int
 */
int select_master_server(uint8_t *key, uint16_t keylen)
{
   uint8_t tblkey;
   int ltid;

   /* If there is only one locktable, we don't need to do the math below to
    * know that this lock will go onto that locktable.
    */
   if( gulm_config.how_many_lts == 1 ) return 0;

   /* look into some other way of deciding which table gets which keys. */
   tblkey = fourtoone(crc32(key, keylen, 0x6d696b65));

   for(ltid=0; ltid < gulm_config.how_many_lts; ltid ++) {
      if( tblkey >= MastersList[ltid].start &&
          tblkey < MastersList[ltid].stop ) {
         break;
      }
   }
   /* now, since the loop above avoids overlap, it also missed anyone with
    * a tblkey of 255, so this below is now wrong.  It is one more than it
    * should be.  But we'll just generalize the whole thing and do a MIN().
    */
   return MIN(ltid, (gulm_config.how_many_lts - 1));
}

/**
 * store_and_forward_lock_state - 
 * @idx: 
 * 
 * from a client to a master lt
 * 
 * Returns: int
 */
int store_and_forward_lock_state(int idx)
{
   int err, ltid;
   uint8_t *x_name=NULL;
   xdr_dec_t *dec = poller.dec[idx];
   xdr_enc_t *enc = poller.enc[idx];
   lock_req_t *lq;

   if( (lq=get_new_lock_req()) == NULL ) {
         die(ExitGulm_NoMemory, "Out of memory\n");
   }
   lq->code = gulm_lock_state_req;
   lq->poll_idx = idx;

   do{
      if((err = xdr_dec_raw_m(dec, (void**)&lq->key, &lq->keylen)) != 0 ) break;
      if((err = xdr_dec_uint8(dec, &lq->state)) != 0 ) break;
      if((err = xdr_dec_uint32(dec, &lq->flags)) != 0 ) break;
      if( lq->flags & gio_lck_fg_hasLVB ) {
         if((err = xdr_dec_raw_m(dec, (void**)&lq->lvb, &lq->lvblen)) != 0 )
            break;
      }else{
         lq->lvb = NULL;
         lq->lvblen = 0;
      }
   }while(0);
   if( err != 0 ) {
         log_err("XDR error %d:%s\n", err, strerror(err));
   }

   /* which master gets it? */
   ltid = select_master_server(lq->key, lq->keylen);

   /* check for duplicates */
   if( hash_find(MastersList[ltid].pending_reqs, lq->key, lq->keylen)!=NULL ||
         find_in_senders_list(ltid, lq->key, lq->keylen) ){
      do{
         if((err = xdr_enc_uint32(enc, gulm_lock_state_rpl)) != 0 ) break;
         if((err = xdr_enc_raw(enc, lq->key, lq->keylen)) != 0 ) break;
         if((err = xdr_enc_uint8(enc, lq->state)) != 0 ) break;
         if((err = xdr_enc_uint32(enc, lq->flags & ~gio_lck_fg_hasLVB)) != 0 )
            break;
         if((err = xdr_enc_uint32(enc, gio_Err_AlreadyPend)) != 0 ) break;
         if((err = xdr_enc_flush(enc)) != 0 ) break;
      }while(0);
      recycle_lock_req(lq);
      if( err != 0 ) {
         log_err("XDR error %d:%s\n", err, strerror(err));
      }
      goto exit;
   }

   /* add it to the sender list. */
   Qu_EnQu( &MastersList[ltid].senderlist, &lq->ls_list);
   poller.polls[MastersList[ltid].poll_idx].events |= POLLOUT;
   MastersList[ltid].senderlistlen ++;

exit:
   if( x_name != NULL) free(x_name);
   return 0;
}

/**
 * store_and_forward_lock_action - 
 * @idx: 
 * 
 * from a client to a master lt
 * 
 * Returns: int
 */
int store_and_forward_lock_action(int idx)
{
   int err, ltid;
   uint8_t *x_name=NULL;
   xdr_dec_t *dec = poller.dec[idx];
   xdr_enc_t *enc = poller.enc[idx];
   lock_req_t *lq;

   if( (lq=get_new_lock_req()) == NULL ) {
      die(ExitGulm_NoMemory, "Out of memory\n");
   }
   lq->code = gulm_lock_action_req;
   lq->poll_idx = idx;

   do{
      if((err = xdr_dec_raw_m(dec, (void**)&lq->key, &lq->keylen)) != 0 ) break;
      if((err = xdr_dec_uint8(dec, &lq->state)) != 0 ) break;
      if( lq->state == gio_lck_st_SyncLVB ) {
         if((err = xdr_dec_raw_m(dec, (void**)&lq->lvb, &lq->lvblen)) != 0 )
            break;
      }else{
         lq->lvb = NULL;
         lq->lvblen = 0;
      }
   }while(0);
   if( err != 0 ) {
         log_err("XDR error %d:%s\n", err, strerror(err));
   }

   /* which master gets it? */
   ltid = select_master_server(lq->key, lq->keylen);

   /* Cancels don't have replies, so don't bother storing it.
    * besides, the request they're canceling is already there and would
    * cause a hash key collision.
    * */
   if( lq->state == gio_lck_st_Cancel) {
      send_lk_act_req(ltid, lq);
      recycle_lock_req(lq);
      goto exit;
   }

   /* check for dups. */
   if( hash_find( MastersList[ltid].pending_reqs, lq->key, lq->keylen)!=NULL ||
         find_in_senders_list(ltid, lq->key, lq->keylen) ) {
      /* send dup error */
      do{
         if((err = xdr_enc_uint32(enc, gulm_lock_action_rpl)) != 0 ) break;
         if((err = xdr_enc_raw(enc, lq->key, lq->keylen)) != 0 ) break;
         if((err = xdr_enc_uint8(enc, lq->state)) != 0 ) break;
         if((err = xdr_enc_uint32(enc, gio_Err_AlreadyPend)) != 0 ) break;
         if((err = xdr_enc_flush(enc)) != 0 ) break;
      }while(0);
      recycle_lock_req(lq);
      if( err != 0 ) {
         log_err("XDR error %d:%s\n", err, strerror(err));
      }
      goto exit;
   }

   /* add it to the sender list. */
   Qu_EnQu( &MastersList[ltid].senderlist, &lq->ls_list);
   poller.polls[MastersList[ltid].poll_idx].events |= POLLOUT;
   MastersList[ltid].senderlistlen ++;

exit:
   if( x_name != NULL) free(x_name);
   return 0;
}

/**
 * forward_drop_exp - 
 * @idx: 
 * 
 * from a client to all Master LTs
 * 
 * Returns: int
 */
int forward_drop_exp(int idx)
{
   int err, ltid;
   xdr_dec_t *dec = poller.dec[idx];
   lock_req_t *lq;
   uint8_t *x_name=NULL, *x_mask=NULL;
   uint16_t x_ml;

   do{
      if((err = xdr_dec_string(dec, &x_name)) != 0 ) break;
      if((err = xdr_dec_raw_m(dec, (void**)&x_mask, &x_ml)) != 0 ) break;
   }while(0);
   if( err != 0 ) {
         log_err("XDR error %d:%s\n", err, strerror(err));
   }

   for(ltid = 0; ltid < gulm_config.how_many_lts; ltid++) {
      /* create a request for each Master. */
      if( (lq=get_new_lock_req()) == NULL ) {
         die(ExitGulm_NoMemory, "Out of memory\n");
      }
      lq->code = gulm_lock_drop_exp;
      lq->poll_idx = idx;

      /* copy copy copy */
      if( x_name == NULL ) {
         lq->lvb = NULL;
      }else{
         lq->lvb = strdup(x_name);
         if( lq->lvb == NULL ) die(ExitGulm_NoMemory, "Out of memory\n");
      }
      lq->key = malloc(x_ml);
      if( lq->key == NULL ) die(ExitGulm_NoMemory, "Out of memory\n");
      memcpy(lq->key, x_mask, x_ml);
      lq->keylen = x_ml;

      Qu_EnQu( &MastersList[ltid].senderlist, &lq->ls_list);
      poller.polls[MastersList[ltid].poll_idx].events |= POLLOUT;
      MastersList[ltid].senderlistlen ++;
   }

   if(x_name != NULL ) free(x_name);
   if(x_mask != NULL ) free(x_mask);
   return 0;
}

/**
 * forward_drop_all - 
 * @idx: 
 * 
 * From a Master LT to all clients.
 * 
 * Returns: int
 */
int forward_drop_all(int idx)
{
   int i, err;
   xdr_enc_t *enc;
   for(i=0; i <= poller.maxi; i++ ) {
      if( poller.polls[i].fd < 0 ) continue;
      if( poller.type[i] != poll_Client ) continue;
      enc = poller.enc[i];
      do{
         if((err = xdr_enc_uint32(enc, gulm_lock_cb_dropall)) != 0) break;
         if((err = xdr_enc_flush(enc)) != 0) break;
      }while(0);
      if( err != 0 ) {
         log_err("XDR error %d:%s\n", err, strerror(err));
      }
   }
   return 0;
}

char *lkeytohex(uint8_t *key, uint8_t keylen);

/**
 * retrive_and_relpy_lock_state - 
 * @idx: 
 * 
 * from master to client
 * 
 * Returns: int
 */
int retrive_and_relpy_lock_state(int idx)
{
   int err, ltid=-1;
   uint32_t x_flags, x_error;
   uint16_t x_kl, x_ll;
   uint8_t x_st;
   LLi_t *tmp;
   xdr_dec_t *dec = poller.dec[idx];
   /* keep these around. Fewer mallocs == faster */
   static uint8_t *x_key=NULL, *x_lvb=NULL;
   static uint16_t x_kbl=0, x_lbl=0;

   ltid = get_ltid_from_poller_idx(idx);
   if( ltid < 0 ) {
      log_err("Theres not master lt id for poller %d\n", idx);
      goto exit;
   }

#if 0
   /* just moves the delay. doesn't solve it. */
   err=1;
   setsockopt(poller.polls[idx].fd, SOL_TCP, TCP_QUICKACK, &err, sizeof(int));
#endif

   do{
      if((err = xdr_dec_raw_ag(dec, (void**)&x_key, &x_kbl, &x_kl)) != 0) break;
      if((err = xdr_dec_uint8(dec, &x_st)) != 0) break;
      if((err = xdr_dec_uint32(dec, &x_flags)) != 0) break;
      if((err = xdr_dec_uint32(dec, &x_error)) != 0) break;
      if( x_flags & gio_lck_fg_hasLVB)
         if((err = xdr_dec_raw_ag(dec, (void**)&x_lvb, &x_lbl, &x_ll)) != 0)
            break;
   }while(0);
   if( err != 0 ) {
         log_err("XDR error %d:%s\n", err, strerror(err));
      goto exit;
   }

   /* lookup/delete from hashmap.
    * if not there, drop.
    * if there, forward reply to lq->poll_idx
    */
   tmp = hash_del(MastersList[ltid].pending_reqs, x_key, x_kl);
   MastersList[ltid].pendreqcnt --;
   if( tmp != NULL ) {
      lock_req_t *lq;
      xdr_enc_t *enc;

      lq = LLi_data(tmp);
      enc = poller.enc[ lq->poll_idx ];
      if( enc == NULL ) {
         /* FIXME Ummm, what to do here? */
         log_err("Client left before getting reply.\n");
         recycle_lock_req(lq);
         goto exit;
      }

      do{
         if((err = xdr_enc_uint32(enc, gulm_lock_state_rpl)) != 0 ) break;
         if((err = xdr_enc_raw(enc, x_key, x_kl)) != 0 ) break;
         if((err = xdr_enc_uint8(enc, x_st)) != 0 ) break;
         if((err = xdr_enc_uint32(enc, x_flags)) != 0 ) break;
         if((err = xdr_enc_uint32(enc, x_error)) != 0 ) break;
         if( x_flags & gio_lck_fg_hasLVB)
            if((err = xdr_enc_raw(enc, x_lvb, x_ll)) != 0 ) break;
         if((err = xdr_enc_flush(enc)) != 0 ) break;
      }while(0);
      if( err != 0 ) {
         log_err("XDR error %d:%s\n", err, strerror(err));
         goto exit;
      }

      recycle_lock_req(lq);
   }

exit:
   return 0;
}


/**
 * retrive_and_relpy_lock_action - 
 * @idx: 
 * 
 * from master to client
 * 
 * Returns: int
 */
int retrive_and_relpy_lock_action(int idx)
{
   int err, ltid;
   uint32_t x_error;
   uint16_t x_kl;
   uint8_t x_st;
   LLi_t *tmp;
   xdr_dec_t *dec = poller.dec[idx];
   static uint8_t *x_key=NULL;
   static uint16_t x_kbl=0;

   ltid = get_ltid_from_poller_idx(idx);
   if( ltid < 0 ) {
      log_err("Theres not master lt id for poller %d\n", idx);
      goto exit;
   }

   do{
      if((err = xdr_dec_raw_ag(dec, (void**)&x_key, &x_kbl, &x_kl)) != 0) break;
      if((err = xdr_dec_uint8(dec, &x_st)) != 0) break;
      if((err = xdr_dec_uint32(dec, &x_error)) != 0) break;
   }while(0);
   if( err != 0 ) {
      log_err("XDR error %d:%s\n", err, strerror(err));
      goto exit;
   }

   /* lookup/delete from hashmap.
    * if not there, drop.
    * if there, forward reply to lq->poll_idx
    */
   tmp = hash_del(MastersList[ltid].pending_reqs, x_key, x_kl);
   MastersList[ltid].pendreqcnt --;
   if( tmp != NULL ) {
      lock_req_t *lq = LLi_data(tmp);
      xdr_enc_t *enc = poller.enc[ lq->poll_idx ];
      if( enc == NULL ) {
         /* FIXME Ummm, what to do here? */
         log_err("Client left before getting reply.\n");
         recycle_lock_req(lq);
         goto exit;
      }

      do{
         if((err = xdr_enc_uint32(enc, gulm_lock_action_rpl)) != 0 ) break;
         if((err = xdr_enc_raw(enc, x_key, x_kl)) != 0 ) break;
         if((err = xdr_enc_uint8(enc, x_st)) != 0 ) break;
         if((err = xdr_enc_uint32(enc, x_error)) != 0 ) break;
         if((err = xdr_enc_flush(enc)) != 0 ) break;
      }while(0);
      if( err != 0 ) {
         log_err("XDR error %d:%s\n", err, strerror(err));
         goto exit;
      }

      recycle_lock_req(lq);
   }

exit:
   return 0;
}


/**
 * forward_cb_to_some_clients - 
 * @idx: 
 * 
 * from master to clients
 *
 * this function will not work if this get threaded. (so don't do that.)
 * 
 * Returns: int
 */
int forward_cb_to_some_clients(int idx)
{
   int i, err;
   uint16_t x_kl;
   uint8_t x_st;
   xdr_dec_t *dec = poller.dec[idx];
   xdr_enc_t *enc;
#ifdef TIMECALLBACKS
   int64_t res;
   uint64_t x_time;
   struct timeval tv;
#endif

   /* keep these around. Fewer mallocs == faster */
   static uint8_t *x_key=NULL;
   static uint16_t x_kbl=0;

#ifdef TIMECALLBACKS
   gettimeofday(&tv, NULL);
#endif

   do{
      if((err = xdr_dec_raw_ag(dec, (void**)&x_key, &x_kbl, &x_kl)) != 0 )
         break;
      if((err = xdr_dec_uint8(dec, &x_st)) != 0 ) break;
#ifdef TIMECALLBACKS
      if((err = xdr_dec_uint64(dec, &x_time)) != 0) break;
#endif
   }while(0);
   if( err != 0 ) {
      log_err("XDR error %d:%s\n", err, strerror(err));
      goto exit;
   }

#if 0
   /* this seems to just increase the delay. (as well as just moving it.)
    */
   do{
      xdr_enc_uint32(poller.enc[idx], gulm_nop);
      xdr_enc_flush(poller.enc[idx]);
   }while(0);
#endif

#ifdef TIMECALLBACKS
   res = tvs2uint64(tv) - x_time;
   if( res > 5000 ) {
      log_msg(lgm_Always, "Long send time for callback packet. "
            "%"PRIu64" - %"PRIu64" = %lld\n",
            tvs2uint64(tv), x_time, res);
   }
#endif

   /* should look into ways of making this loop faster.
    * maybe it shouldn't be a loop?
    */
   for(i=0; i <= poller.maxi; i++) {
      if( poller.polls[i].fd < 0 ) continue;
      if( poller.type[i] != poll_Client ) continue;
      if( poller.space[i].key != NULL &&
            /* this cmp is not correct. or is it? seems to be ok */
          memcmp(poller.space[i].key, x_key, MIN(poller.space[i].len,x_kl))!=0
        ) {
         log_msg(lgm_Always, "Skipping client %s for lock %s\n",
               poller.ipn[i].name, lkeytohex(x_key, x_kl));
         continue;
      }

      enc = poller.enc[i];
      do{
         if((err = xdr_enc_uint32(enc, gulm_lock_cb_state)) != 0 ) break;
         if((err = xdr_enc_raw(enc, x_key, x_kl)) != 0 ) break;
         if((err = xdr_enc_uint8(enc, x_st)) != 0 ) break;
         if((err = xdr_enc_flush(enc)) != 0 ) break;
      }while(0);
      if( err != 0 ) {
         log_err("XDR error %d:%s\n", err, strerror(err));
         goto exit;
      }
   }

exit:
   return 0;
}

/*****************************************************************************/

/**
 * do_login - 
 * @idx: 
 * 
 */
static void do_login(int idx)
{
   int err;
   uint32_t x_vers, rpl_err=gio_Err_Ok;
   uint8_t *x_name, x_ama;
   xdr_dec_t *dec = poller.dec[idx];
   xdr_enc_t *enc = poller.enc[idx];

   do{
      if((err = xdr_dec_uint32(dec, &x_vers)) != 0 ) break;
      if((err = xdr_dec_string(dec, &x_name)) != 0 ) break;
      if((err = xdr_dec_uint8(dec, &x_ama)) != 0 ) break;
   }while(0);
   if( err != 0 ) {
      log_err("xdr error %d while trying to read login\n", err);
      close_by_idx(idx);
      goto exit;
   }

   if( ! IN6_IS_ADDR_LOOPBACK(poller.ipn[idx].ip.s6_addr32) ) {
      /* XXX will I have to check for v4 loopback as well? */
      log_err("Only connections from localhost are allowed."
            " You're from %s\n",
            print_ipname(&poller.ipn[idx]));
      err = gio_Err_NotAllowed;
   }else
   if( GIO_WIREPROT_VERS != x_vers) {
      log_msg(lgm_Network, "Wrong protocol version\n");
      rpl_err = gio_Err_BadWireProto;
   }else
   if( gio_lck_st_Client != x_ama ) {
      log_err(" Only clients are allowed to connect here.\n");
      rpl_err = gio_Err_BadLogin;
   }

   do{
      if((err=xdr_enc_uint32(enc, gulm_lock_login_rpl)) != 0 ) break;
      if((err=xdr_enc_uint32(enc, rpl_err)) != 0 ) break;
      /* always return Master here, even though that is not true.
       * this is residue from the old way of things.  Once everything gets
       * more stablaized in the new way, this will goaway or get replaced.
       */
      if((err=xdr_enc_uint8(enc, gio_Mbr_ama_Master)) != 0 ) break;
      err = xdr_enc_flush(enc);
   }while(0);
   if( err != 0 ) {
      log_err("Got %d sending reply to new login %s\n", err, x_name);
      close_by_idx(idx);
      goto exit;
   }else
   if( rpl_err == gio_Err_Ok ) {
      log_msg(lgm_Network2, "New Locker \"%s\" connected. idx:%d fd:%d\n",
            x_name, idx, poller.polls[idx].fd);

      if( poller.ipn[idx].name != NULL ) free(poller.ipn[idx].name);
      poller.ipn[idx].name = strdup(x_name);
      poller.state[idx] = poll_Open;
      poller.times[idx] = 0;
      poller.type[idx] = poll_Client;
   }else
   {
      log_msg(lgm_Network2, "We gave %s an error (%d:%s)\n", x_name,
            rpl_err, gio_Err_to_str(rpl_err));
      close_by_idx(idx);
   }


exit:
   if( x_name != NULL ) {free(x_name); x_name = NULL;}
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


   errno = 0;
   err = xdr_dec_uint32(dec, &code);
   if( err == -EPROTO ) {
      log_msg(lgm_Network, "EOF on xdr (%s idx:%d fd:%d)\n",
            print_ipname(&poller.ipn[idx]),
            idx, poller.polls[idx].fd);
      close_by_idx(idx);
      return;
   }
   if( err != 0 ) {
      if( errno == 0 ) errno = err;
      log_msg(lgm_Always, "Error on xdr (%s): %d:%d:%s\n", 
            print_ipname(&poller.ipn[idx]),
            err, errno, strerror(abs(errno)));
      close_by_idx(idx);
      return;
   }

   /* from local clients. */
   if( gulm_lock_login_req == code ) {
      do_login(idx);
   }else
   if( gulm_lock_logout_req == code ) {
      xdr_enc_uint32(enc, gulm_lock_logout_rpl);
      xdr_enc_flush(enc);
      close_by_idx(idx);
   }else
   if( gulm_lock_sel_lckspc == code ) {
      if( poller.space[idx].key != NULL ) {
         free(poller.space[idx].key);
         poller.space[idx].key = 0;
      }
      err = xdr_dec_raw_m(dec, (void**)&poller.space[idx].key,
                          &poller.space[idx].len);
      if( err != 0 ) {
         poller.space[idx].key = NULL;
         poller.space[idx].len = 0;
         log_err("Got xdr error %d reading lockspace filter\n", err);
      }
   }else
   if( gulm_lock_state_req == code ) {
      store_and_forward_lock_state(idx);
   }else
   if( gulm_lock_action_req == code ) {
      store_and_forward_lock_action(idx);
   }else
   if( gulm_lock_drop_exp == code ) {
      forward_drop_exp(idx);
   }else
   /* from the masters */
   if( gulm_lock_state_rpl == code ) {
      retrive_and_relpy_lock_state(idx);
   }else
   if( gulm_lock_action_rpl == code ) {
      retrive_and_relpy_lock_action(idx);
   }else
   if( gulm_lock_cb_state == code ) {
      forward_cb_to_some_clients(idx);
   }else
   if( gulm_lock_cb_dropall == code ) {
      forward_drop_all(idx);
   }else
   /* from our core */
   if( gulm_core_mbr_updt == code ) {
      struct in6_addr x_ip;
      uint8_t x_cur_state=-1;
      do {
         if((err = xdr_dec_string(dec, &x_name)) != 0 ) break;
         if((err=xdr_dec_ipv6(dec, &x_ip)) != 0 ) break;
         if((err = xdr_dec_uint8(dec, &x_cur_state)) != 0 ) break;
      }while(0);
      if( err == 0 ) {
         log_msg(lgm_Subscribers, "Recvd mbrupdt: %s, %s:%#x\n",
               x_name, gio_mbrupdate_to_str(x_cur_state), x_cur_state);

         if( x_cur_state == gio_Mbr_Expired ||
             x_cur_state == gio_Mbr_Logged_out ) {
            if( MasterIN.name != NULL ) {
               if(IN6_ARE_ADDR_EQUAL(x_ip.s6_addr32, MasterIN.ip.s6_addr32) ||
                  strcmp(x_name, MasterIN.name) == 0 ) {
                  /* Master Died! */
                  log_msg(lgm_Network2, "Master node reported dead.\n");
                  close_all_masters();
                  I_am_the = gio_Mbr_ama_Pending;
               }
            }
            if( strcmp(myName, x_name) == 0 ) {
               log_msg(lgm_Network2, "Core is shutting down.\n");
               /* or should this get done differently? */
               SIGTERM_TRIPPED = TRUE;
            }
         }

      }
      if( x_name != NULL ) {free(x_name); x_name = NULL;}
   }else
   if( gulm_core_state_chgs == code ) {
      uint8_t x_st;
      struct in6_addr x_ip;
      do {
         if((err=xdr_dec_uint8(dec, &x_st)) != 0 ) break;
         if( x_st == gio_Mbr_ama_Slave ) {
            if((err=xdr_dec_ipv6(dec, &x_ip)) != 0 ) break;
            if((err=xdr_dec_string(dec, &x_name)) != 0 ) break;
         }
      }while(0);
      if( err != 0 ) {
         log_err("Failed to recv Core state update! %s\n", strerror(errno));
      }else{
         log_msg(lgm_ServerState, "New State: %s\n", gio_I_am_to_str(x_st));

         if( x_st == gio_Mbr_ama_Slave ) {
            if( I_am_the == gio_Mbr_ama_Slave ){
               /* nothing to change.
                * Need to figure out why doubles come through.
                * */
            }else{
               if(!IN6_ARE_ADDR_EQUAL(x_ip.s6_addr32, MasterIN.ip.s6_addr32)) {
                  /* drop possible previous connection to master. */
                  close_all_masters();

                  /* copy in master info. */
                  if( MasterIN.name != NULL ) {
                     free(MasterIN.name);
                     MasterIN.name = NULL;
                  }
                  MasterIN.name = strdup(x_name);
                  if( MasterIN.name == NULL )
                     die(ExitGulm_NoMemory,"Out of Memory.\n");
                  memcpy(&MasterIN.ip, &x_ip, sizeof(struct in6_addr));
                  log_msg(lgm_Always, "New Master at %s\n",
                        print_ipname(&MasterIN));

                  /* in the main loop, it will detect that this needs to
                   * loginto the Master, and will start the dirty work there.
                   */
               }
            }
         }else
         if( x_st == gio_Mbr_ama_Pending ) {
            /* sit idle. */
            close_all_masters();
         }else
         { /* x_st == gio_Mbr_ama_Master || gio_Mbr_ama_Arbitrating */
            /* not slave, so the master is on us. */
            if( x_st != I_am_the ) { /* only if the state changed. */
               if(!IN6_ARE_ADDR_EQUAL(myIP.s6_addr32, MasterIN.ip.s6_addr32)) {
                  /* drop possible previous connection to master. */
                  close_all_masters();

                  if( MasterIN.name != NULL ) {
                     free(MasterIN.name);
                     MasterIN.name = NULL;
                  }
                  MasterIN.name = strdup(myName);
                  if( MasterIN.name == NULL )
                     die(ExitGulm_NoMemory,"Out of Memory.\n");
                  memcpy(&MasterIN.ip, &myIP, sizeof(struct in6_addr));
                  log_msg(lgm_Always, "New Master at %s\n",
                        print_ipname(&MasterIN));
               }
            }
         }

         I_am_the = x_st;
      }
      if( x_name != NULL ) {free(x_name); x_name = NULL;}
   }else
   /* from gulm_tool */
   if( gulm_info_stats_req == code ) {
      xdr_enc_uint32(enc, gulm_info_stats_rpl);
      xdr_enc_list_start(enc);
      send_io_stats(enc);
      xdr_enc_list_stop(enc);
      xdr_enc_flush(enc);
   }else
   if( gulm_info_set_verbosity == code ) {
      /* wiether this xdr fails or succeds, the socket is closed, so we do
       * not need to check its error code.
       */
      if( xdr_dec_string(dec, &x_name) == 0 ) {
      set_verbosity(x_name, &verbosity);
         if( x_name != NULL ) {free(x_name); x_name = NULL;}
      }
      close_by_idx(idx);
   }else
   if( gulm_socket_close == code ) {
      close_by_idx(idx);
   }else
   /* from the wrong place. */
   if( gulm_err_reply == code ) {
      uint32_t x_nc, x_err;
      do{
         if((err=xdr_dec_uint32(dec, &x_nc))!=0) break;
         if((err=xdr_dec_uint32(dec, &x_err))!=0) break;
      }while(0);
      if( err != 0 ) {
      }
      log_err("We got an error code %d:%s on command %#x;%s from %s\n",
            x_err, gio_Err_to_str(x_err),
            x_nc, gio_opcodes(x_nc),
            print_ipname(&poller.ipn[idx])
            );
   }else
   {
      log_err("Unexpected op code %#x (%s), on fd:%d name:%s\n",
            code, gio_opcodes(code), poller.polls[idx].fd,
            print_ipname(&poller.ipn[idx]));

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
   return 0;
}

/**
 * ltpx_main_loop - 
 *
 * This loop handles incommings.
 * 
 * Returns: int
 */
void ltpx_main_loop(void)
{
   int cnt,i;
   int NoRead = FALSE;
   uint64_t tryskip = 0;

   initialize_MastersList();
   gettimeofday(&Started_at, NULL);
   gettimeofday(&NOW, NULL);
   get_core_state();

   while( !SIGTERM_TRIPPED) {

      /* If we're not logged into a Master, and we're supposed to be,
       * better get our butts hooked up.
       */
      if( MasterIN.name != NULL &&
          !IN6_IS_ADDR_UNSPECIFIED(MasterIN.ip.s6_addr32) &&
          tryskip + 1000000 < tvs2uint64(NOW) &&
          ( I_am_the == gio_Mbr_ama_Slave ||
            I_am_the == gio_Mbr_ama_Master ) ) {
         tryskip = tvs2uint64(NOW);
         checkup_masters();
      }

      if( (cnt = poll(poller.polls, poller.maxi +1, 1000)) <= 0) {
         if( cnt < 0 && errno != EINTR )
            log_err("poll error: %s\n",strerror(errno));
         if(SIGTERM_TRIPPED) return;
      }
      gettimeofday(&NOW, NULL);

      for( i=0; i <= poller.maxi ; i++) {
         if( poller.polls[i].fd < 0) continue;
         if( poller.polls[i].revents & POLLHUP ) {
            close_by_idx(i);
         }
         if (poller.polls[i].revents & POLLNVAL ) {
            close_by_idx(i);
         }
         if( poller.polls[i].revents & POLLIN ) {
            if( poller.polls[i].fd == poller.listenFD ) {
               accept_connection();
            }else
            {
               if( poller.state[i] == poll_Trying ) {
                  if( recv_Masterlogin_reply(i) != 0 )
                     close_by_idx(i);
               }else {
                  /* ok, when the senderlist gets to a certain fullness,
                   * need to stop reading from clients.
                   */
                  if( poller.type[i] != poll_Client ||
                      NoRead == FALSE )
                     recv_some_data(i);
               }
            }
         }
         /* check for timed out pollers. */
         if( poller.times[i] != 0 &&
             poller.times[i]+ gulm_config.new_con_timeout < tvs2uint64(NOW)) {
            log_msg(lgm_Network, "Timeout (%"PRIu64") on fd:%d (%s)\n",
                  gulm_config.new_con_timeout, poller.polls[i].fd, 
                  print_ipname(&poller.ipn[i]));
            close_by_idx(i);
         }
         if(SIGTERM_TRIPPED) return;
      }/*for( i=0; i <= poller.maxi ; i++)*/

      /* send things out. */
      NoRead = FALSE;
      for(i=0; i < gulm_config.how_many_lts; i++ ) {
         if( MastersList[i].poll_idx > -1 &&
             poller.state[MastersList[i].poll_idx] == poll_Open &&
             poller.polls[MastersList[i].poll_idx].revents & POLLOUT ) {
            /* inspect senderslist, if over certain amount, set NoRead
             * 
             * Should work such that any Master's sender list gets too full
             * and all reading from all clients stops.
             */
            if( MastersList[i].senderlistlen > 1000 ) {
               NoRead = TRUE;
            }
            send_senderlist(i);
         }
      } /*for(i=0; i < gulm_config.how_many_lts; i++ )*/

   }/* while(!SIGTERM_TRIPPED) */
}

/* vim: set ai cin et sw=3 ts=3 : */
