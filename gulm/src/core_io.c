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
#include "gio_wiretypes.h"
#include "xdr.h"
#include "core_priv.h"
#include "config_gulm.h"
#include "utils_ip.h"
#include "utils_tostr.h"
#include "utils_verb_flags.h"

/*****************************************************************************/
/* First data that is stored in the main. */

/* bits of data used by the log_*() and die() functions. */
extern uint32_t verbosity;
extern char *ProgramName;

/* signal checks. */
extern int SIGCHLD_TRIPPED;
/* if a service locks us, we will not shutdown until they log out. */
unsigned int shutdown_locked = 0;
static int running = TRUE; /* this daemon runs. */
/* confed things. */
extern gulm_config_t gulm_config;
extern char myName[256];
extern struct in6_addr myIP;

/* Then data that is private to this file */
static uint64_t last_hb_reply = 0;
static int master_missed = 0;
static struct timeval Started_at;
static struct timeval NOW;

static ip_name_t *MasterIN = NULL;
int I_am_the = gio_Mbr_ama_Pending;/* what state we are in */
static int MyRank = -1;
static int quorumcount = 1; /* how many before we go.  We count as one. */
static uint8_t quorate = FALSE;
static uint64_t GenerationID = 0;

typedef enum {poll_Closed = 0,
   poll_Accepting,  /* For new incomming sockets. */
   poll_Connecting, /* For new outgoing connecting sockets. */
   poll_Trying,     /* For connected Outgoing sockets (not yet logged in). */
   poll_Open} poll_state;
typedef enum {
   poll_Nothing = 0, /* Unused poller. */
   poll_New,         /* New poller that we're not sure of the type yet. */
   poll_Internal,    /* Listen socket for example */
   poll_Slave,       /* Could be a Master someday */
   poll_Client,      /* not in servers=[] */
   poll_Resource
}poll_type;
struct {
   struct pollfd *polls;
   xdr_enc_t **enc;
   xdr_dec_t **dec;
   poll_state *state;
   poll_type *type;
   uint64_t *times;
   ip_name_t *ipn;
   unsigned int maxi;
   int listenFD; /* socket for new connections. */
   int listenIDX; /* mostly for if I am poking around with gdb. */
   int MasterIDX; /* If we're a slave, where the Master is. */
} poller;

/* On trying all of the possible master servers at once instead of single
 * stepping through the list.
 *
 * i think if I turn this into an array of the masters, I could get things
 * to query each one at the same time.
 *
 * some function to set all elements to starting state.  Then call
 * master_probe_top() on each element.
 *  master_probe_connected() get called, then maybe master_probe_middle()
 *  ending with master_probe_bottom()
 *
 * mpb() needs to mark this element as done. (no matter what state)
 * then ....
 *
 * crappy. mpm() will do the slave login if it finds a master.  So if that
 * happens, we need to stop the others.  But not just that, we need to pick
 * the highest ranked one if there are multiple.  oooooo evil.
 * 
 * This may take a bit more work than I originally thought....
 */
struct {
   int try_again;
   ip_name_t *LastMasterIN;
   LLi_t *current;
   int testing;
   uint64_t lasttry;
} Login_state;

/* This is only true when we are first starting up the server.  This is
 * because GenerationID miss-matches can be handled differently durring
 * this time.  Since we know that we have never had quorum and thus we've
 * never had usable state; we can reset things without restarting the app.
 *
 * Once we are Slave or Master, we are no longer in startup.
 */
int startup = TRUE;

/**
 * login_setup - 
 * 
 * initialize the master scanning structs.  Needs to be done when ever we
 * enter the Pending state.
 * 
 */
void login_setup(void)
{
   Login_state.try_again = FALSE;
   Login_state.LastMasterIN = MasterIN;
   Login_state.current = NULL;
   Login_state.testing = FALSE;
   Login_state.lasttry = 0;
   I_am_the = gio_Mbr_ama_Pending;
   log_msg(lgm_ServerState, "In state: %s\n", gio_I_am_to_str(I_am_the));
   send_core_state_to_children();
   quorumcount = 1;

   Login_state.current = LLi_next(&gulm_config.node_list);
}


/**
 * init_core_poller - 
 * 
 * Returns: int
 */
int init_core_poller(void)
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

   poller.enc = malloc(open_max() * sizeof(xdr_enc_t*));
   if( poller.enc == NULL ) goto nomem;

   poller.dec = malloc(open_max() * sizeof(xdr_dec_t*));
   if( poller.dec == NULL ) goto nomem;

   for(i=0; i < open_max(); i++) {
      poller.polls[i].fd = -1;
      poller.polls[i].events = 0;
      poller.state[i] = poll_Closed;
      poller.type[i] = poll_Nothing;
      poller.times[i] = 0;
      memset(&poller.ipn[i].ip, 0, sizeof(struct in6_addr));
      poller.ipn[i].name = NULL;
      poller.enc[i] = NULL;
      poller.dec[i] = NULL;
   }

   poller.maxi = 0;
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
   return -ENOMEM;
}

/**
 * release_core_poller - 
 * @oid: 
 * 
 * 
 * Returns: void
 */
void release_core_poller(void)
{
   int i;
   if(poller.polls) free(poller.polls);
   if(poller.state) free(poller.state);
   if(poller.type) free(poller.type);
   if(poller.times) free(poller.times);
   for(i=0; i < open_max(); i ++ ) {
      if( poller.enc[i] != NULL )
         xdr_enc_release(poller.enc[i]);
      if( poller.dec[i] != NULL )
         xdr_dec_release(poller.dec[i]);
      if( poller.ipn[i].name != NULL )
         free(poller.ipn[i].name);
   }
   if(poller.ipn) free(poller.ipn);
   if(poller.enc) free(poller.enc);
   if(poller.dec) free(poller.dec);
}

/**
 * add_to_pollers - 
 * @fd: 
 * @state: 
 * @time: 
 * @name: 
 * @ip: 
 * 
 * 
 * Returns: int
 */
static int add_to_pollers(int fd, int state, uint64_t time,
                          const char *name, const struct in6_addr *ip)
{
   int i;
   for(i=0; poller.polls[i].fd >=0 && i< open_max(); i++);
   if( i>= open_max() ) return -1;

   if(fcntl(fd, F_SETFD, FD_CLOEXEC ) <0) return -1; /* close on exec. */

   poller.polls[i].fd = fd;
   poller.polls[i].events = POLLIN;
   if(i> poller.maxi) poller.maxi = i;
   poller.state[i] = state;
   poller.type[i] = poll_New;
   poller.times[i] = time;
   memcpy(&poller.ipn[i].ip, ip, sizeof(struct in6_addr));
   if( name != NULL ) poller.ipn[i].name = strdup(name);
   else poller.ipn[i].name = NULL;
   poller.enc[i] = NULL;
   poller.dec[i] = NULL;
   /* you need to do the xdr seperate. */

   return i;
}

/**
 * print_poller_entry - 
 * @idx: 
 * 
 * 
 */
static void print_poller_entry(int idx)
{
   char *s="";
#define CasedString(x)  case (x):  s = #x ; break;
   log_msg(lgm_Always, "poller idx         = %d\n", idx);
   log_msg(lgm_Always, "polls[].fd         = %d\n", poller.polls[idx].fd);
   log_msg(lgm_Always, "polls[].events     = %x\n", poller.polls[idx].events);
   log_msg(lgm_Always, "polls[].revents    = %x\n", poller.polls[idx].revents);
   switch(poller.state[idx]){
      CasedString(poll_Closed);
      CasedString(poll_Accepting);
      CasedString(poll_Connecting);
      CasedString(poll_Trying);
      CasedString(poll_Open);
   }
   log_msg(lgm_Always, "state[]            = %s\n", s);
   switch(poller.type[idx]){
      CasedString(poll_Nothing);
      CasedString(poll_New);
      CasedString(poll_Internal);
      CasedString(poll_Slave);
      CasedString(poll_Client);
      CasedString(poll_Resource);
   }
   log_msg(lgm_Always, "type[]             = %s\n", s);
   log_msg(lgm_Always, "times[]            = %"PRId64"\n", poller.times[idx]);
   log_msg(lgm_Always, "ipn[].name         = %s\n", poller.ipn[idx].name);
   log_msg(lgm_Always, "ipn[].ip           = %s\n",
         ip6tostr(&poller.ipn[idx].ip));
   log_msg(lgm_Always, "enc[]              = %p\n", poller.enc[idx]);
   log_msg(lgm_Always, "dec[]              = %p\n", poller.dec[idx]);
#undef CasedString
}

/**
 * add_xdr_to_poller - 
 * @idx: 
 * 
 * 
 * Returns: int
 */
static int add_xdr_to_poller(int idx)
{
   if( idx < 0 ) return idx;
   poller.enc[idx] = xdr_enc_init( poller.polls[idx].fd, 512);
   if( poller.enc[idx] == NULL ) return -ENOMEM;
   poller.dec[idx] = xdr_dec_init( poller.polls[idx].fd, 512);
   if( poller.dec[idx] == NULL ) {
      xdr_enc_release(poller.enc[idx]);
      poller.enc[idx] = NULL;
      return -ENOMEM;
   }
   return 0;
}

/**
 * in_servers_list_ip - 
 * @ip: 
 * 
 * 
 * Returns: int
 */
int in_servers_list_ip(struct in6_addr *ip)
{
   LLi_t *tmp;
   ip_name_t *in;

   for(tmp=LLi_next(&gulm_config.node_list);
       NULL != LLi_data(tmp);
       tmp = LLi_next(tmp) ) {
      in = LLi_data(tmp);
      if(IN6_ARE_ADDR_EQUAL(in->ip.s6_addr32 , ip->s6_addr32)) return TRUE;
   }
   return FALSE;
}

/**
 * decrement_quorumcount - 
 */
void decrement_quorumcount(void)
{
   quorumcount --;
   if( I_am_the == gio_Mbr_ama_Master &&
       quorumcount < gulm_config.quorum){
      log_msg(lgm_Network, "Core lost slave quorum. Have %d, need %d. "
            "Switching to Arbitrating.\n",
            quorumcount, gulm_config.quorum);
      I_am_the = gio_Mbr_ama_Arbitrating;
      quorate = FALSE;
      log_msg(lgm_ServerState, "In state: %s\n", gio_I_am_to_str(I_am_the));
      send_quorum_to_slaves();
      send_core_state_to_children();
      set_nodes_mode(myName, I_am_the);

   }
}

/**
 * close_by_idx - 
 * @idx: 
 * 
 * 
 * Returns: 
 */
void close_by_idx(int idx)
{
   if( idx < 0 || idx > open_max() ) return;
   log_msg(lgm_Network2, "Closing connection idx:%d, fd:%d to %s\n",
         idx, poller.polls[idx].fd, poller.ipn[idx].name);
   /* If we just closed the connect to the Master, set things up to try to
    * re-find it.
    * gotta do this before we wipe out the info.
    */

   if( poller.MasterIDX != -1 && idx == poller.MasterIDX ) {
      poller.MasterIDX = -1;
      log_msg(lgm_Network2, "Connection to Master closed.\n");
      if( gulm_config.fog ) {
         login_setup(); /* this sets I_am_the to Pending. */
      }else{
         /* not fog, we'll never get our lockstate back, so... */
         die(ExitGulm_SelfKill,
               "Lost connection to SLM Master (%s), stopping. "
               "node reset required to re-activate cluster operations.\n",
               poller.ipn[idx].name);
      }
   }
   GULMD_ASSERT( poller.polls[idx].fd != poller.listenFD, 
         print_poller_entry(idx);
            );

   if( poller.type[idx] == poll_Slave )
      decrement_quorumcount();
   if( poller.type[idx] == poll_Resource )
      release_resource(poller.ipn[idx].name);

   close( poller.polls[idx].fd );
   poller.polls[idx].fd = -1;
   poller.polls[idx].revents = 0; /* clear any other events. */
   poller.state[idx] = poll_Closed;
   poller.type[idx] = poll_Nothing;
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
}

/**
 * close_slaves - 
 * 
 * This needs to close all connections to Slaves, and none else.
 */
void close_slaves(void)
{
   int i;
   log_msg(lgm_Network2, "Closing any Slave connections.\n");
   for(i=0; i < open_max(); i++) {
      if( poller.type[i] == poll_Slave || poller.type[i] == poll_Client )
         close_by_idx(i);
   }
}

/*****************************************************************************/
/**
 * send_io_stats - 
 * @enc: 
 * 
 * 
 * Returns: int
 */
static int send_io_stats(xdr_enc_t *enc)
{
   struct timeval tv;
   char *s, tmp[256] = "1: Why are you looking in this binary?";

   xdr_enc_string(enc, "I_am");
   xdr_enc_string(enc, gio_I_am_to_str(I_am_the));

   if( MasterIN != NULL ) {
      xdr_enc_string(enc, "Master");
      xdr_enc_string(enc, MasterIN->name);
   }else{
      xdr_enc_string(enc, "quorum_has");
      snprintf(tmp, 256, "%d", quorumcount);
      xdr_enc_string(enc, tmp);

      xdr_enc_string(enc, "quorum_needs");
      snprintf(tmp, 256, "%d", gulm_config.quorum);
      xdr_enc_string(enc, tmp);
   }

   xdr_enc_string(enc, "rank");
   snprintf(tmp, 256, "%d", MyRank);
   xdr_enc_string(enc, tmp);

   xdr_enc_string(enc, "quorate");
   snprintf(tmp, 256, "%s", quorate?"true":"false");
   xdr_enc_string(enc, tmp);

   xdr_enc_string(enc, "GenerationID");
   snprintf(tmp, 256, "%"PRIu64, GenerationID);
   xdr_enc_string(enc, tmp);

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

   xdr_enc_string(enc, "failover");
   if( gulm_config.fog )
      s = "enabled";
   else
      s = "disabled";
   xdr_enc_string(enc, s);
      

   return 0;
}

/**
 * open_core_listener - 
 * @port: 
 * 
 * 
 * Returns: int
 */
int open_core_listener(int port)
{
   int i;
   poller.listenFD = serv_listen(port);
   if( poller.listenFD < 0 ) return -1;
   i = add_to_pollers(poller.listenFD, poll_Open, 0, "_ listener _",
         &in6addr_any);
   poller.type[i] = poll_Internal;
   poller.listenIDX = i;
   /* no xdr on the listener socket. */
   return 0;
}

/**
 * accept_connection - 
 * 
 * Returns: int
 */
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
      log_err("Failed to attatch xdr to new socket due to lack of memory.\n");
      close_by_idx(i);
      return -1;
   }

   return 0;
}

/**
 * master_probe_top - 
 * 
 * 
 * Returns: int
 */
static int master_probe_top(void)
{
   struct sockaddr_in6 adr;
   ip_name_t *in;
   int idx, cmFD;

   Login_state.lasttry = tvs2uint64(NOW);
   in = LLi_data(Login_state.current);
   if( in == NULL ) return -1;

   /* don't try to loginto myself. */
   if( IN6_ARE_ADDR_EQUAL(in->ip.s6_addr32, myIP.s6_addr32) ||
       strcmp(in->name, myName) == 0 )
      return -1;

   log_msg(lgm_LoginLoops, "Looking for Master server at %s %#x\n",
         print_ipname(in), gulm_config.corePort);

   /* socket connect to CM */
   if((cmFD = socket(AF_INET6, SOCK_STREAM, 0)) <0) return -1;

   memset(&adr, 0, sizeof(struct sockaddr_in6));
   adr.sin6_family = AF_INET6;
   memcpy(&adr.sin6_addr, &in->ip, sizeof(struct in6_addr));
   adr.sin6_port = htons(gulm_config.corePort);

   idx = add_to_pollers(cmFD, poll_Connecting, tvs2uint64(NOW),
         in->name, &in->ip);
   if( idx < 0 ) { /* out of free FDs. */
      log_err("Failed to find unused poller space.\n");
      close(cmFD);
      return -1;
   }

   log_msg(lgm_LoginLoops, "Trying to connect to possible Master "
         "%s idx:%d fd:%d\n", print_ipname(in),
         idx, cmFD );

   /* set socket to non-blocking */
   if(fcntl(cmFD, F_SETFL, O_NONBLOCK) < 0 ) {
      log_err("Cannot set Nonblock on new socket. %d:%s\n", errno,
            strerror(errno));
      close_by_idx(idx);
      return -1;
   }

   errno = 0;
   connect(cmFD, (struct sockaddr*)&adr, sizeof(struct sockaddr_in6));
   if( errno != EINPROGRESS ) {
      log_msg(lgm_LoginLoops, "Cannot connect to %s %d (%s)\n",
            print_ipname(in), gulm_config.corePort, strerror(errno));
      close_by_idx(idx);
      return -1;
   }

   /* when we can write, there is a connect. */
   poller.polls[idx].events = POLLOUT;

   Login_state.testing = TRUE;
   return 0;
}

/**
 * master_probe_connected - 
 * 
 * ok, we connected, so now send off login request.
 * 
 * Returns: int
 */
static int master_probe_connected(int idx)
{
   int err;
   int sock_error;
   socklen_t solen = sizeof(int);
   xdr_enc_t *xdr;

   /* check the error codes. */
   if( getsockopt(poller.polls[idx].fd, SOL_SOCKET, SO_ERROR,
            &sock_error, &solen ) < 0) {
      log_err("Failed to get socket error off of master connect %d:%s\n",
            errno, strerror(errno));
      goto fail;
   }
   if( sock_error != 0 ) {
      log_msg(lgm_LoginLoops, "Cannot connect to %s %d (%d:%s)\n",
            print_ipname( &poller.ipn[idx]),
            gulm_config.corePort, sock_error, strerror(sock_error));
      goto fail;
   }
   
   /* set socket to blocking. */
   if(fcntl(poller.polls[idx].fd, F_SETFL, 0) < 0 ) {
      log_err("Cannot set block on new socket. %d:%s\n", errno,
            strerror(errno));
      goto fail;
   }

   if( set_opts(poller.polls[idx].fd) <0) {
      log_msg(lgm_LoginLoops, "Failed to set options (%s)\n", strerror(errno));
      goto fail;
   }

   if( add_xdr_to_poller(idx) < 0 ) {
      log_err("Failed to allocate memory for xdr.\n");
      goto fail;
   }
   xdr = poller.enc[idx];

   /* send login request */
   log_msg(lgm_LoginLoops, "Sending login request to possible Master "
         "%s idx:%d fd:%d\n",
         print_ipname(&poller.ipn[idx]),
         idx, poller.polls[idx].fd);

   do {
      if((err = xdr_enc_uint32(xdr, gulm_core_login_req)) != 0) break;
      if((err = xdr_enc_uint32(xdr, GIO_WIREPROT_VERS)) != 0) break;
      if((err = xdr_enc_string(xdr, gulm_config.clusterID)) != 0) break;
      if((err = xdr_enc_string(xdr, myName)) != 0) break;
      if((err = xdr_enc_uint64(xdr, GenerationID)) != 0) break;
      if((err = xdr_enc_uint32(xdr, gulm_config.hashval)) != 0) break;
      if((err = xdr_enc_uint32(xdr, MyRank)) != 0) break;
      if((err = xdr_enc_flush(xdr)) != 0) break;
   }while(0);
   if( err != 0 ) {
      log_msg(lgm_LoginLoops, "Errors trying to send login request. %d:%s\n",
            err, strerror(errno));
      goto fail;
   }

   Login_state.testing = TRUE;

   /* now we want to know about data to read. */
   poller.polls[idx].events = POLLIN;
   poller.state[idx] = poll_Trying;
   poller.times[idx] = tvs2uint64(NOW);

   return 0;
fail:
   Login_state.testing = FALSE;
   close_by_idx(idx);
   return -1;
}

/**
 * master_probe_bottom - 
 * 
 * 
 * Returns: int
 */
static int master_probe_bottom(void)
{
   Login_state.current = LLi_next(Login_state.current);
   Login_state.testing = FALSE;

   if( LLi_data(Login_state.current) == NULL &&
       I_am_the == gio_Mbr_ama_Pending ) {
      /* we have walked through the list.
       * If MyRank == -1, then I cannot be a Master.
       * */
      if( Login_state.try_again || MyRank == -1 ) {
         Login_state.try_again = FALSE;
         Login_state.current = LLi_next(&gulm_config.node_list);
         return 0;
      }

      /* Not trying again, so we must be the new master. */
      if( gulm_config.quorum == 1 ) {
         I_am_the = gio_Mbr_ama_Master;
         startup = FALSE;
         log_msg(lgm_ServerState, "In state: %s\n", gio_I_am_to_str(I_am_the));
         log_msg(lgm_Network,
               "I see no Masters, So I am becoming the Master.\n");
         quorate = TRUE;
      }else{
         I_am_the = gio_Mbr_ama_Arbitrating;
         log_msg(lgm_ServerState, "In state: %s\n", gio_I_am_to_str(I_am_the));
         log_msg(lgm_Network,
               "I see no Masters, So I am Arbitrating until enough Slaves "
               "talk to me.\n");
         quorate = FALSE;
      }
      send_quorum_to_slaves();
      send_core_state_to_children();
      MasterIN = NULL; /* we are the Master now */
      master_missed = 0; /* reset this, not that it should ever be looked
                          * at again. but hey, you never know. */
      last_hb_reply = 0;

      if( GenerationID == 0 ) {
         /* Brand new instance of the servers. */
         GenerationID = tvs2uint64(NOW); /* ??good enough?? yes */
         log_msg(lgm_Network, "New generation of server state. (%"PRIu64")\n",
               GenerationID);
      }

      /* if there was an old master, fence them. */
      if( Login_state.LastMasterIN != NULL ) {
         /* Need to mark old Master as Expired. */
         log_msg(lgm_Stomith, "LastMaster %s, is being marked Expired.\n",
               print_ipname(Login_state.LastMasterIN));

         Mark_Expired(Login_state.LastMasterIN->name);
         send_mbrshp_to_children(Login_state.LastMasterIN->name,
               gio_Mbr_Expired);
         send_mbrshp_to_slaves(Login_state.LastMasterIN->name,
               gio_Mbr_Expired);
      }

      /* kill all already expired nodes
       * this could end up in a double fencing of some nodes, but that is
       * perfered to not being fenced at all.
       * (Master fences, and before the results are propigated to slaves,
       *  Master dies.  Then the slave that becomes new master will call
       *  fence again.)
       * */
      fence_all_expired();

      /* keep from killing everyone because we didn't have valid heartbeat
       * times. (heartbeat times are not tracked in slaves, so everything
       * in the nodes list on a slave has 'timed out' when it first becomes
       * arbit.)
       */
      beat_all_once();

      /* Need to make sure that we are not marked as expired.
       * If we are marked as expired, just die.
       */
      Die_if_expired(myName);

      /* Move all old Logins to the transisional logged in state.
       * This is an inbetween state that lets a node that was logged in log
       * back in.  It is only needed for when a slave becomes Master, since
       * all the nodes will be in the logged in state, and that isn't quite
       * accurate anymore.  So we put them into the "was logged in but lost
       * connection but probably doesn't need fencing" state.  (which is
       * much harder to say than oldmaster state or transisional login)
       */
      Mark_Old_Master_lgin();

      /* Make sure we're logged in too.  This is mostly just to get the
       * Master node into the nodelist along with the others.
       */
      add_node(myName, &myIP);
      Mark_Loggedin(myName);
      set_nodes_mode(myName, I_am_the);

      return 1;
   }
   return 0;
}

/**
 * master_probe_middle - 
 * @idx: 
 * 
 * 
 * Returns: int
 */
static int master_probe_middle(int idx)
{
   xdr_dec_t *xdr = poller.dec[idx];
   xdr_enc_t *enc = poller.enc[idx];
   uint64_t generation=0;
   uint32_t code=0;
   uint32_t rpl_err=1; /* if you ever see this, non of the recv got called. */
   uint32_t rpl_rank=0;
   uint8_t rpl_ama=0;
   int err;

   if(xdr == NULL ) return -1;

   /* recv login reply */
   do{
      if((err = xdr_dec_uint32(xdr, &code)) != 0) break;
      if((err = xdr_dec_uint64(xdr, &generation)) != 0) break;
      if((err = xdr_dec_uint32(xdr, &rpl_err)) != 0) break;
      if((err = xdr_dec_uint32(xdr, &rpl_rank)) != 0) break;
      if((err = xdr_dec_uint8(xdr, &rpl_ama)) != 0) break;
   } while(0);

   if( rpl_err == 0 && err == 0 ) {
      switch(rpl_ama) {
         case gio_Mbr_ama_Slave:
            log_msg(lgm_Network2, "mpm: they(%s) are a Slave.\n",
                  poller.ipn[idx].name);
            close_by_idx(idx);
            break;
         case gio_Mbr_ama_Arbitrating:
            if( MyRank < 0 ) {
               /* I am in client only mode, and so cannot log into an
                * Arbitrating server.
                *
                * TODO Thinking about changing this.  Need to figure out
                * everything first though.
                * Thinking that it might be much wiser to let client cores
                * connect to an arbit.  they won't effect quorum, and if
                * that arbit decides to become a slave, it will kick them
                * all out and they will rescan anyways.
                *
                * well, core/jid is doing what I want, but the ltpx/lock
                * stuff is flooding trying to loginto the master which
                * isn't a master yet.  So do I change that? or try
                * something else?
                *
                * Still doesn't solve the case where for some other reason
                * there are no clients logged in to recv the fenced
                * messages to start journal replay.  I Need some other
                * method.
                */
               log_msg(lgm_Network2, "mpm: they(%s) are Arbitrating. "
                     "I'll check them again later.\n",
                     poller.ipn[idx].name);
               /* XXX bleh. should still deserialize node list. */
               close_by_idx(idx);
               break;
            }else
            if( I_am_the == gio_Mbr_ama_Arbitrating ) {
               /* if the node we connected to is Arbitrating, and we are also
                * Arbitrating, we need to check to see if they rank us.  If
                * they do, we give up Arbit, and go Slave.
                * Else continue on, and they'll do that soon enough.
                */
               if( MyRank < rpl_rank ) break;
               /* MyRank is smaller, thus I'm closer to the top of the
                * list.  Thus I out rank them.
                */
               /* kick out any slaves that might be connected to me.
                * Need to do a logout on them as well.
                *
                * We should not have any clients logged in, but might under
                * some netsplit type conditions.  Ok, but they need to be
                * kicked out as well anyways.
                * */
               /* gotta set type here quick, else this gets closed. */
               poller.type[idx] = poll_Internal;
               close_slaves();
               release_nodelist();/*behaves the same as logging everyone out.*/
               /* XXX calling release here might be wrong.
                * I think calling it here will prevent the tag_for_lost()
                * from working.
                * Need to investigate.
                */
            }

         case gio_Mbr_ama_Master:
            /* do generation checking here */
            if( GenerationID != 0 && GenerationID < generation ) {
               /* if my genid is smaller, then we don't want to connect
                * to this arbit/mast
                *
                * So keep searching.
                */
               /* clients die when genids go bad. bug #173 */
               if( MyRank < 0 ) {
                  die(ExitGulm_SelfKill,
                        "GenerationID missmatch: "
                        "me:%"PRIu64" they:%"PRIu64"\n",
                        GenerationID, generation);
               }
               if( startup ) {
                  /* We have never had quorum and thus never had useable
                   * state.  So we can flip back to pending with GenID 0
                   * and keep going without issue.
                   */
                  log_msg(lgm_Always, "GenerationID missmatch: "
                     "me:%"PRIu64" they:%"PRIu64" "
                     "In startup, reseting. Continuing to scan.\n",
                   GenerationID, generation);
                  GenerationID = 0;
                  I_am_the = gio_Mbr_ama_Pending;
                  Login_state.try_again = TRUE;
               } else {
                  log_msg(lgm_Always, "GenerationID missmatch: "
                        "me:%"PRIu64" they:%"PRIu64" "
                        "Continuing to scan.\n",
                      GenerationID, generation);
               }

               /* send logout */
               xdr_enc_uint32(enc, gulm_core_logout_req);
               xdr_enc_string(enc, myName);
               xdr_enc_uint8(enc, I_am_the);
               xdr_enc_flush(enc);
               close_by_idx(idx);
               break;
            }
            /* 
             * The idea here is that we(the resources) really want to have
             * the slave core tell us if a node was removed from the
             * nodelist while it wasn't connected to a master.  So when it
             * reconncets to a Master node, it needs to see which nodes are
             * not in the Master's list, but are still in its. and then
             * tell us(the resources) about them.
             *
             * So its mark who is here. receive nodelist (overwrites),
             * check who didn't get overwritten.
             */
            tag_for_lost();
            if( (err = deserialize_node_list(xdr)) != 0 ) {
               log_err("Failed to deserialize initial Node list from "
                       "Master %s (%d:%d:%s)\n", poller.ipn[idx].name,
                       err, errno, strerror(errno));
               close_by_idx(idx);
            }else{
               /* Check for nodes that are tagged, these are the nodes that
                * either died or logged out while we were Masterless.
                * Remove them from our list, and tell our children they're
                * gone.
                */
               Logout_leftovers();

               poller.MasterIDX = idx;
               poller.state[idx] = poll_Open;
               poller.type[idx] = poll_Internal; /* connection to Master. */
               poller.times[idx] = 0;
               I_am_the = gio_Mbr_ama_Slave;
               startup = FALSE;
               log_msg(lgm_ServerState, "In state: %s\n",
                     gio_I_am_to_str(I_am_the));
               GenerationID = generation; /* slaves copy master's gen. */
               master_missed = 0; /* cannot have missed any if not there */
               last_hb_reply = NOW.tv_sec;
               MasterIN = LLi_data(Login_state.current);
               if( MyRank < 0 ) {/* mmmSugar */
                  log_msg(lgm_Network,
                        "Found Master at %s, so I'm a Client.\n",
                        MasterIN->name);
               }else{
                  log_msg(lgm_Network,
                        "Found Master at %s, so I'm a Slave.\n",
                        MasterIN->name);
               }
               send_core_state_to_children();
               /* TODO
                * mostly a post 5.2 thing.
                * Check to see if LastMaster is in the list.  If it is not,
                * send a Killed message to resources.
                * umm, really?
                * lets try.
                * Uh, this looks like it conflicts with Logout_leftovers().
                */
               if( Login_state.LastMasterIN != NULL ) {
                  /* the lookup is just an easy test to see if the node is
                   * in our lists.
                   */
                  send_mbrshp_to_children(Login_state.LastMasterIN->name,
                     gio_Mbr_Killed);
               }
               return 1;
            }
            break;
         case gio_Mbr_ama_Pending:

            if( MyRank > rpl_rank ) Login_state.try_again = TRUE;

            log_msg(lgm_Network2, "mpm: They(%s) are Pending.\n",
                  poller.ipn[idx].name);
            close_by_idx(idx);
            break;
         default:
            log_msg(lgm_LoginLoops,"Unknown ama(%d) returned\n", rpl_ama);
            close_by_idx(idx);
            /* this is an error case. */
            break;
      }
   } else {
      if( rpl_err != 0 ) {
         log_err("Got error from reply: (%s) %d:%s\n",
               print_ipname(&poller.ipn[idx]),
               rpl_err, gio_Err_to_str(rpl_err));
      }
      if( err < 0 ) {
         log_err("Errors on xdr: (%s) %d:%d:%s\n",
               print_ipname(&poller.ipn[idx]),
               err, errno, strerror(errno));
      }
      /* On some of these errors, should we die instead of trying again?
       * Like the bad config crc...
       * actually, any of the gio_Err_Bad*
       * really?
       */
      close_by_idx(idx);
      Login_state.try_again = TRUE;
      if( rpl_err == gio_Err_BadGeneration && startup ) {
         /* In startup, we can recover from this without dieing.
          */
         GenerationID = 0;
         I_am_the = gio_Mbr_ama_Pending;
         log_msg(lgm_Always, "In startup, reseting. Continuing to scan.\n");
      }else
      if( rpl_err == gio_Err_BadGeneration ||
          rpl_err == gio_Err_BadCluster    ||
          rpl_err == gio_Err_BadConfig
        ) {
         die(ExitGulm_SelfKill, "Error of type %d:%s encountered.  "
               "For the Sanity of the cluster I am stopping.\n",
               rpl_err, gio_Err_to_str(rpl_err));
      }
   }

   return master_probe_bottom();
}

/*****************************************************************************/

/**
 * send_update - 
 * @enc: 
 * @name: 
 * @st: 
 * 
 * 
 * Returns: int
 */
int send_update(int poll_idx, char *name, int st, struct in6_addr *ip)
{
   int e;
   xdr_enc_t *enc;
   if( poll_idx < 0 || poll_idx > open_max() ) return -EINVAL;
   enc = poller.enc[poll_idx];
   if( enc == NULL ) return -EINVAL;

   if((e = xdr_enc_uint32(enc, gulm_core_mbr_updt)) != 0) return e;
   if((e = xdr_enc_string(enc, name)) != 0) return e;
   if((e = xdr_enc_ipv6(enc, ip)) != 0) return e;
   if((e = xdr_enc_uint8(enc, st)) != 0) return e;
   if((e = xdr_enc_flush(enc)) != 0) return e;
   return 0;
}

/**
 * send_quorum - 
 * @poll_idx: 
 * 
 * 
 * Returns: int
 */
int send_quorum(int poll_idx)
{
   int e;
   xdr_enc_t *enc;
   if( poll_idx < 0 || poll_idx > open_max() ) return -EINVAL;
   enc = poller.enc[poll_idx];
   if( enc == NULL ) return -EINVAL;

   if((e = xdr_enc_uint32(enc, gulm_core_quorm_chgs)) != 0) return e;
   if((e = xdr_enc_uint8(enc, quorate)) != 0) return e;
   return 0;
}

/**
 * send_core_state_update - 
 * @enc: 
 * 
 * 
 * Returns: int
 */
int send_core_state_update(int poll_idx)
{
   int err;
   xdr_enc_t *enc;
   if( poll_idx < 0 || poll_idx > open_max() ) return -EINVAL;
   enc = poller.enc[poll_idx];
   if( enc == NULL ) return -EINVAL;

   if((err=xdr_enc_uint32(enc, gulm_core_state_chgs)) !=0 ) return err;
   if((err=xdr_enc_uint8(enc, I_am_the)) !=0 ) return err;
   if((err=xdr_enc_uint8(enc, quorate)) != 0 ) return err;
   if( I_am_the == gio_Mbr_ama_Slave ) {
      if( MasterIN == NULL )
         log_err("MasterIN is NULL!!!!!!!\n");
      if((err=xdr_enc_ipv6(enc, &MasterIN->ip)) !=0 ) return err;
      if((err=xdr_enc_string(enc, MasterIN->name)) !=0 ) return err;
   }
   if((err=xdr_enc_flush(enc)) !=0 ) return err;
   return 0;
}

/**
 * switch_into_Pending - 
 * 
 * This is how we transition from any state into Pending.  It is a reset of
 * sorts.  This should be able to take the server from any of S,A,M and
 * move cleanly into Pending.
 * 
 */
void switch_into_Pending(void)
{
   switch(I_am_the) {
      case gio_Mbr_ama_Master:
      case gio_Mbr_ama_Arbitrating:
         /* do logout. */
         send_mbrshp_to_slaves(myName, gio_Mbr_Logged_out);
         release_nodelist();/*behaves the same as logging everyone out.*/
         close_slaves();
         login_setup();/* restart the Pending State */
         break;
      case gio_Mbr_ama_Slave:
         {
            xdr_enc_t *enc;
            enc = poller.enc[poller.MasterIDX];
            xdr_enc_uint32(enc, gulm_core_logout_req);
            xdr_enc_string(enc, myName);
            xdr_enc_uint8(enc, I_am_the);
            xdr_enc_flush(enc);
         }
         release_nodelist();
         close_by_idx(poller.MasterIDX);
         /* closing the masterIDX calls login_setup()
          * which correctly switches our state to Pending.
          * which also informs resources that we're in pending.
          */
         break;
      case gio_Mbr_ama_Pending:
         /* umm, duh. */
         return;
      default:
         break;
   }
   /* we need to re-add ourself to the nodelist. */
   add_node(myName, &myIP);
   Mark_Loggedin(myName);
   set_nodes_mode(myName, I_am_the);
}

/**
 * do_resource_login - 
 * @idx: 
 * 
 * 
 * Returns: int
 */
static void do_resource_login(int idx)
{
   uint32_t x_proto, x_opt;
   uint8_t *x_clusterID = NULL, *x_name = NULL;
   int err = 0, e=0;
   xdr_dec_t *dec = poller.dec[idx];
   xdr_enc_t *enc = poller.enc[idx];

   do { /* recv rest of login request */
      if((err = xdr_dec_uint32(dec, &x_proto)) != 0) break;
      if( x_proto != GIO_WIREPROT_VERS) {
         err=gio_Err_BadWireProto;
         log_err("Protocol Mismatch: We're %#x and They're %#x\n",
               GIO_WIREPROT_VERS, x_proto);
         break;
      }
      if((err = xdr_dec_string(dec, &x_clusterID)) != 0) break;
      if((err = xdr_dec_string(dec, &x_name)) != 0) break;
      if((err = xdr_dec_uint32(dec, &x_opt)) != 0) break;
   } while(0);
   if( err != 0 ) {
      log_err("Failed to recv all of the service login packet. %d:%s\n",
            err, (err<1000)?strerror(err):gio_Err_to_str(err));
      close_by_idx(idx);
      goto exit;
   }

   err = gio_Err_Ok;
   if( ! IN6_IS_ADDR_LOOPBACK(poller.ipn[idx].ip.s6_addr32) ) {
      /* XXX will I have to check for v4 loopback as well? */
      log_err("Services cannot connect from anything other than localhost."
            " You're from %s\n",
            print_ipname(&poller.ipn[idx]));
      err = gio_Err_NotAllowed;
   }else
   if( x_clusterID != NULL && strcmp(x_clusterID, gulm_config.clusterID)!=0) {
      log_err("%s claims to be part of %s, but we are %s\n",
            poller.ipn[idx].name, x_clusterID, gulm_config.clusterID);
      err = gio_Err_BadCluster;
   }else
   if( add_resource(x_name, idx, x_opt) != 0 ) {
      log_err("There is already a service named \"%s\" here.\n", x_name);
      err = gio_Err_BadLogin;
   }

   do{
      if((e = xdr_enc_uint32(enc, gulm_core_login_rpl)) != 0) break;
      if((e = xdr_enc_uint64(enc, GenerationID)) != 0) break;
      if((e = xdr_enc_uint32(enc, err)) != 0) break;
      if((e = xdr_enc_uint32(enc, MyRank)) != 0) break;
      if((e = xdr_enc_uint8(enc, I_am_the)) != 0) break;
      if((e = xdr_enc_flush(enc)) != 0) break;
   }while(0);
   if( e != 0 ) {
      log_err("Got %d sending reply to service %s\n", e, x_name);
      close_by_idx(idx);
      goto exit;
   }
   if( err == gio_Err_Ok ) {
      log_msg(lgm_Network2, "New Service \"%s\" connected. idx:%d fd:%d\n",
            x_name, idx, poller.polls[idx].fd);

      if( poller.ipn[idx].name != NULL ) free(poller.ipn[idx].name);
      poller.ipn[idx].name = strdup(x_name);
      poller.state[idx] = poll_Open;
      poller.times[idx] = 0;
      poller.type[idx] = poll_Resource;

   }else{
      log_msg(lgm_Network2, "We gave service (%s) an error (%d:%s).\n",
            x_name, err, gio_Err_to_str(err));
      close_by_idx(idx);
   }

exit:
   if( x_clusterID != NULL ) {free(x_clusterID); x_clusterID = NULL;}
   if( x_name != NULL ) {free(x_name); x_name = NULL;}
}

/**
 * do_new_login - 
 * @idx: 
 * 
 */
static void do_new_login(int idx)
{
   uint64_t x_generation;
   uint32_t x_config_crc, x_rank, x_proto;
   uint8_t *x_clusterID = NULL, *x_name = NULL;
   int err = 0, e=0;
   xdr_dec_t *dec = poller.dec[idx];
   xdr_enc_t *enc = poller.enc[idx];

   do { /* recv rest of login request */
      if((err = xdr_dec_uint32(dec, &x_proto)) != 0) break;
      if( x_proto != GIO_WIREPROT_VERS) {
         err=gio_Err_BadWireProto;
         log_err("Protocol Mismatch: We're %#x and They're %#x\n",
               GIO_WIREPROT_VERS, x_proto);
         break;
      }
      if((err = xdr_dec_string(dec, &x_clusterID)) != 0) break;
      if((err = xdr_dec_string(dec, &x_name)) != 0) break;
      if((err = xdr_dec_uint64(dec, &x_generation)) != 0) break;
      if((err = xdr_dec_uint32(dec, &x_config_crc)) != 0) break;
      if((err = xdr_dec_uint32(dec, &x_rank)) != 0) break;
   } while(0);
   if( err != 0 ) {
      log_err("Failed to recv all of the login packet. %d:%s\n",
            err, (err<1000)?strerror(err):gio_Err_to_str(err));
      close_by_idx(idx);
      goto exit;
   }

   if( I_am_the == gio_Mbr_ama_Slave ||
       I_am_the == gio_Mbr_ama_Pending ) {
      /* we don't let login, but a pender or arbitrater may be scanning us,
       * so we need to tell them we are a slave.  So we do the reply packet
       * here, then just close the connection skipping the rest of the login.
       */
      do {
         if((e = xdr_enc_uint32(enc, gulm_core_login_rpl)) != 0) break;
         if((e = xdr_enc_uint64(enc, GenerationID)) != 0) break;
         if((e = xdr_enc_uint32(enc, 0)) != 0) break;
         if((e = xdr_enc_uint32(enc, MyRank)) != 0) break;
         if((e = xdr_enc_uint8(enc, I_am_the)) != 0) break;
         if((e = xdr_enc_flush(enc)) != 0) break;
      } while(0);
      if( e != 0 ) {
         log_err("Got %d sending slave reply to %s\n", e, x_name);
      }
      log_msg(lgm_Network2, "dnl: We are a %s. Telling %s to go away.\n",
            gio_I_am_to_str(I_am_the), x_name);
      close_by_idx(idx);
      goto exit;
   }

   /* compare name and ip.
    * compare cluster ID
    * if generation != 0, is it correct?
    * compare config crc.
    * add node to list.
    * Mark logged in
    * beat node.
    * send reply
    * adjust quorum if needed.
    * bcast login
    */
   if( verify_name_and_ip(x_name, &poller.ipn[idx].ip) == 0 ) {
      err = gio_Err_NotAllowed;
      log_err("Node (%s %s) has been denied from connecting here.\n",
            x_name, ip6tostr(&poller.ipn[idx].ip));
   }else
   if( strncmp(x_clusterID, gulm_config.clusterID, CLUSTERIDLEN) != 0 ) {
      err = gio_Err_BadCluster;
      log_msg(lgm_Always, "This is cluster \"%s\", not \"%s\"\n",
              gulm_config.clusterID, x_clusterID);
   }else
   if( x_generation != 0 && x_generation > GenerationID ) {
      /* iif  genid is the realtime of when this started,
       * then the smaller genid is the one that should win.
       * XXX this may not be correct anymore.
       */
      err = gio_Err_BadGeneration;
      log_msg(lgm_Always, "Generation ID of client(%s) (%"PRIu64") is not "
             "the same as ours (%"PRIu64")\n", x_name,
             x_generation, GenerationID);
#if 0
      /* mantis thinks we should fence here.  */
      queue_node_for_fencing(x_name);
#endif
   }else
   if( x_config_crc != 0 && x_config_crc != gulm_config.hashval ) {
      err = gio_Err_BadConfig;
      log_msg(lgm_Always, "Config CRC doesn't match. ( %u != %u )\n",
            x_config_crc, gulm_config.hashval);
   }else
   if(0 != add_node(x_name, &poller.ipn[idx].ip)) {
      err = gio_Err_MemoryIssues;
   }else
   if( (err=Mark_Loggedin(x_name)) != gio_Err_Ok ) {
      log_msg(lgm_Network," (%s %s) Cannot login if you are expired.\n", 
            x_name, ip6tostr(&poller.ipn[idx].ip));
   }else
   if( (err=beat_node(x_name, idx)) != gio_Err_Ok) {
      log_err("Failed to heartbeat node. (%s %s)\n",
              x_name, ip6tostr(&poller.ipn[idx].ip));
   }

   do {
      if((e = xdr_enc_uint32(enc, gulm_core_login_rpl)) != 0) break;
      if((e = xdr_enc_uint64(enc, GenerationID)) != 0) break;
      if((e = xdr_enc_uint32(enc, err)) != 0) break;
      if((e = xdr_enc_uint32(enc, MyRank)) != 0) break;
      if((e = xdr_enc_uint8(enc, I_am_the)) != 0) break;
      if((e = xdr_enc_flush(enc)) != 0) break;
   } while(0);

   if( e != 0 ) {
      log_err("Errors sending login reply! %d:%s\n", errno, strerror(errno));
      close_by_idx(idx);
      goto exit;
   }
   /* if we returned an error to them, stop. */
   if( err != 0 ) {
      log_msg(lgm_Network2, "We gave them(%s) an error (%d:%s).\n",
            x_name, err, gio_Err_to_str(err));
      close_by_idx(idx);
      goto exit;
   }

   /* incr quorum if this client counts.
    * set type too.
    * and mode. (want mode before serialize...)
    */
   if( in_servers_list_ip(&poller.ipn[idx].ip) ) {
      poller.type[idx] = poll_Slave;
      quorumcount ++;
      set_nodes_mode(x_name, gio_Mbr_ama_Slave);
   }else
   {
      poller.type[idx] = poll_Client;
      set_nodes_mode(x_name, gio_Mbr_ama_Client);
   }

   if( I_am_the == gio_Mbr_ama_Arbitrating ) {
      if( quorumcount >= gulm_config.quorum ) {
         log_msg(lgm_Network, "Now have Slave quorum, going full Master.\n");
         I_am_the = gio_Mbr_ama_Master;
         startup = FALSE;
         quorate = TRUE;
         log_msg(lgm_ServerState, "In state: %s\n", gio_I_am_to_str(I_am_the));
         /* cannot send quorum to slaves here, must wait until after
          * deserialize.
          */
         send_core_state_to_children();
         set_nodes_mode(myName, I_am_the);
      }else{
         log_msg(lgm_Network, "Still in Arbitrating: Have %d, need "
               "%d for quorum.\n", quorumcount, gulm_config.quorum);
      }
   }

   /* If I am the master or arbitrating, send the serialization of the node
    * list to the slave/client.
    */
   if( serialize_node_list(enc) != 0 ) {
      log_err("Failed to send serialization of node list.\n");

      Mark_Loggedout(x_name); /* ? really do this here ? */

      close_by_idx(idx);
      goto exit;
   }

   if( poller.ipn[idx].name != NULL ) free(poller.ipn[idx].name);
   poller.ipn[idx].name = strdup(x_name);
   poller.state[idx] = poll_Open;
   poller.times[idx] = 0;

   /* If they are a client, and we are not Master, close it.
    */
   if( I_am_the != gio_Mbr_ama_Master && poller.type[idx] == poll_Client ) {
      Mark_Loggedout(x_name);
      close_by_idx(idx);
      goto exit;
   }

   log_msg(lgm_Network,"New Client: idx:%d fd:%d from %s\n",
         idx, poller.polls[idx].fd,
         print_ipname(&poller.ipn[idx]));

   send_quorum_to_slaves();
   send_mbrshp_to_slaves(x_name, gio_Mbr_Logged_in);
   send_mbrshp_to_children(x_name, gio_Mbr_Logged_in);

exit:
   if( x_clusterID != NULL ) {free(x_clusterID); x_clusterID = NULL;}
   if( x_name != NULL ) {free(x_name); x_name = NULL;}
}

/**
 * recv_some_data - 
 * @idx: 
 * 
 */
static void recv_some_data(int idx)
{
   xdr_dec_t *dec = poller.dec[idx];
   xdr_enc_t *enc = poller.enc[idx];
   uint32_t code=0;
   uint32_t x_error;
   uint8_t *x_name = NULL;
   uint8_t x_ama;
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

      /* die in a way that this node will get fenced.
       * I think i should be doing this check in other places.... XXX
       * */
      if( poller.type[idx] == poll_Resource &&
          die_with_me(poller.ipn[idx].name) ) {
         die(ExitGulm_SelfKill, "Cannot continue without \"%s\" "
               "Node reset required to re-activate cluster operations.\n",
               poller.ipn[idx].name);
      }
      close_by_idx(idx);
      return;
   }
   if( err != 0 ) {
      if( errno == 0 ) errno = err;
      log_msg(lgm_Always, "Error on xdr (%s idx:%d fd:%d): (%d:%d:%s)\n", 
            print_ipname(&poller.ipn[idx]),
            idx, poller.polls[idx].fd, err, errno,
            strerror(abs(errno)));
      close_by_idx(idx);
      return;
   }

   if( code == gulm_core_login_req ) {
      do_new_login(idx);
   }else
   if( code == gulm_core_reslgn_req ) {
      do_resource_login(idx);
   }else
   if( code == gulm_core_login_rpl ) {
      uint64_t x_generation;
      uint32_t x_rank;
      xdr_dec_uint64(dec, &x_generation);
      xdr_dec_uint32(dec, &x_error);
      xdr_dec_uint32(dec, &x_rank);
      xdr_dec_uint8(dec, &x_ama);
   }else
   if( code == gulm_core_logout_req ) {
      do {
         if((err=xdr_dec_string(dec, &x_name)) != 0) break;
         if((err=xdr_dec_uint8(dec, &x_ama)) != 0) break;
      }while(0);
      if( err == 0 ) {

         if( x_ama == gio_Mbr_ama_Resource ) {
            release_resource(x_name);

            /* reply */
            xdr_enc_uint32(enc, gulm_core_logout_rpl);
            xdr_enc_uint32(enc, 0);
            xdr_enc_flush(enc);

            log_msg(lgm_Network,"\"%s\" is logged out. fd:%d\n", x_name,
                  poller.polls[idx].fd);

            if( x_name != NULL ) {free(x_name); x_name = NULL;}

         }else
         {

            Mark_Loggedout(x_name);

            /* reply */
            xdr_enc_uint32(enc, gulm_core_logout_rpl);
            xdr_enc_uint32(enc, 0);
            xdr_enc_flush(enc);

            log_msg(lgm_Network,"\"%s\" is logged out. fd:%d\n", x_name,
                  poller.polls[idx].fd);
            send_mbrshp_to_slaves(x_name, gio_Mbr_Logged_out);
            send_mbrshp_to_children(x_name, gio_Mbr_Logged_out);

            if( x_name != NULL ) {free(x_name); x_name = NULL;}
         }
      }
      close_by_idx(idx);
   }else
   if( code == gulm_core_logout_rpl ) {
      /* just eat it and toss. */
      xdr_dec_uint32(dec, &x_error);
   }else
   if( code == gulm_core_beat_req ) {
      do {
         if((err=xdr_dec_string(dec, &x_name)) != 0 ) break;
      } while(0);
      if( err == 0 ) {

      /* make sure that idx isn't the listener */
      GULMD_ASSERT( idx != poller.listenIDX , );
      x_error = beat_node(x_name, idx);

      xdr_enc_uint32(enc, gulm_core_beat_rpl);
      xdr_enc_uint32(enc, x_error);
      xdr_enc_flush(enc);

         if( x_name != NULL ) {free(x_name); x_name = NULL;}
      }
   }else
   if( code == gulm_core_beat_rpl ) {
      /* I ignore this field, so I'm ignoring the results of the xdr. */
      xdr_dec_uint32(dec, &x_error);

      last_hb_reply = tvs2uint64(NOW);
      master_missed = 0;

   }else
   if( code == gulm_core_state_req ) {
      send_core_state_update(idx);
   }else
   if( code == gulm_core_res_req ) {
      serialize_resources(enc);
   }else
   if( code == gulm_core_mbr_lstreq ) {
      serialize_node_list(enc);
   }else
   if( code == gulm_core_quorm_chgs ) {
      /* should only ever come from master socket */
      if( xdr_dec_uint8(dec, &quorate) == 0 ) {
         send_core_state_to_children();
      }
   }else
   if( code == gulm_core_mbr_req ) {
      if( xdr_dec_string(dec, &x_name) == 0 ) {
         get_node(enc, x_name);
         if( x_name != NULL ) {free(x_name); x_name = NULL;}
      }
   }else
   if( code == gulm_core_mbr_force ) {
      if( xdr_dec_string(dec, &x_name) == 0 ) {
         if( gio_Mbr_ama_Slave == I_am_the ) {
            /* forward to the master/arbit */
            if( poller.MasterIDX > 0 && poller.enc[poller.MasterIDX] != NULL ) {
               do{
                  xdr_enc_t *menc = poller.enc[poller.MasterIDX];
                  if((err =xdr_enc_uint32(menc, gulm_core_mbr_force))!=0) break;
                  if((err = xdr_enc_string(menc, x_name)) != 0) break;
                  if((err = xdr_enc_flush(menc)) != 0) break;
               }while(0);
               xdr_enc_uint32(enc, gulm_err_reply);
               xdr_enc_uint32(enc, gulm_core_mbr_force);
               xdr_enc_uint32(enc, err);
               xdr_enc_flush(enc);
            }else{
               xdr_enc_uint32(enc, gulm_err_reply);
               xdr_enc_uint32(enc, gulm_core_mbr_force);
               xdr_enc_uint32(enc, EINVAL);
               xdr_enc_flush(enc);
            }
         }else {
            if( strcmp(x_name, myName) == 0 ) {
               xdr_enc_uint32(enc, gulm_err_reply);
               xdr_enc_uint32(enc, gulm_core_mbr_force);
               xdr_enc_uint32(enc, gio_Err_Ok);
               xdr_enc_flush(enc);
               log_msg(lgm_Always, "Doing a self kill. Goodby.\n");
               exit(ExitGulm_SelfKill);
            }else{
               err = Force_Node_Expire(x_name);
               xdr_enc_uint32(enc, gulm_err_reply);
               xdr_enc_uint32(enc, gulm_core_mbr_force);
               xdr_enc_uint32(enc, err);
               xdr_enc_flush(enc);
            }
         }
         if( x_name != NULL ) {free(x_name); x_name = NULL;}
      }
   }else
   if( code == gulm_core_mbr_updt ) {
      struct in6_addr x_ip;
      uint8_t x_cur_state;
      do{ 
         if((err=xdr_dec_string(dec, &x_name)) != 0 ) break;
         if((err=xdr_dec_ipv6(dec, &x_ip)) != 0 ) break;
         if((err=xdr_dec_uint8(dec, &x_cur_state)) != 0 ) break;
      }while(0);
      if(err != 0 ) {
         /* bad news here.
          * Our connection to core is probably going bad.
          *
          * this might be a bit of an over-reaction...
          * */
         close_by_idx(idx);
      }else{

         send_mbrshp_to_children(x_name, x_cur_state);

         if( I_am_the == gio_Mbr_ama_Slave ) {
            /* record in case we become master someday. */
            if( x_cur_state == gio_Mbr_Logged_in) {
               if( add_node(x_name, &x_ip) != 0 ) {
               }
               Mark_Loggedin(x_name);
               if( in_servers_list_ip(&x_ip) ) {
                  set_nodes_mode(x_name, gio_Mbr_ama_Slave);
               }else{
                  set_nodes_mode(x_name, gio_Mbr_ama_Client);
               }
            }else
            if( x_cur_state == gio_Mbr_Logged_out) {
               Mark_Loggedout(x_name);
               if( MasterIN != NULL &&
                   ( IN6_ARE_ADDR_EQUAL(x_ip.s6_addr32, MasterIN->ip.s6_addr32)
                     || strcmp(MasterIN->name, x_name) == 0 )
                 ) {
                  log_msg(lgm_Network, "Master Node has logged out.\n");
                  send_mbrshp_to_children(MasterIN->name, gio_Mbr_Logged_out);
                  MasterIN = NULL; /* clear this so old Master isn't fenced. */
                  close_by_idx(idx);
               }
            }else
            if( x_cur_state == gio_Mbr_Expired) {
               Mark_Expired(x_name);
            }else
            if( x_cur_state == gio_Mbr_Killed) {
               Mark_lgout_from_Exp(x_name);
            }
         }

         if( x_name != NULL ) {free(x_name); x_name = NULL;}
      }
   }else
   if( gulm_info_stats_req == code ) {
      xdr_enc_uint32(enc, gulm_info_stats_rpl);
      xdr_enc_list_start(enc);
      send_io_stats(enc);
      xdr_enc_list_stop(enc);
      xdr_enc_flush(enc);
   }else
   if( gulm_info_set_verbosity == code ) {
      if( xdr_dec_string(dec, &x_name) == 0 ) {
      set_verbosity(x_name, &verbosity);
         if( x_name != NULL ) {free(x_name); x_name = NULL;}
      }
      close_by_idx(idx);
   }else
   if( gulm_core_shutdown == code ) {
      /* logout and sutdown.
       * Since this is the same as getting a SIGTERM, we'll just pretend
       * that is exactly what happened.
       */
      log_msg(lgm_Network2, "Received Shutdown request.\n");
      if( shutdown_locked > 0 ) {
         log_msg(lgm_Network, "Cannot shutdown, we are locked.\n");
         err = gio_Err_NotAllowed;
      }else{
         running = FALSE;
         err = gio_Err_Ok;
      }
      xdr_enc_uint32(enc, gulm_err_reply);
      xdr_enc_uint32(enc, gulm_core_shutdown);
      xdr_enc_uint32(enc, err);
      xdr_enc_flush(enc);
   }else
   if( code == gulm_core_forcepend ) {
      switch_into_Pending();
   }else
   if( code == gulm_socket_close ) {
      close_by_idx(idx);
   }else
   if( gulm_err_reply == code ) {
      /* just eat it for now. */
      do{
         if((err = xdr_dec_uint32(dec, &x_error)) != 0 ) break;
         if((err = xdr_dec_uint32(dec, &x_error)) != 0 ) break;
      }while(0);
   }else
   {
      log_err("Unexpected op code %#x (%s), on fd:%d name:%s\n",
            code, gio_opcodes(code),
            poller.polls[idx].fd, poller.ipn[idx].name);
      close_by_idx(idx);
   }
   return;
}

/**
 * send_heartbeat - Send a heartbeat packet.
 */
int send_heartbeat()
{
   struct timeval tv;
   static uint64_t lastrun=0;
   uint64_t fulltime;
   int err;

   gettimeofday(&tv,NULL);
   fulltime = tvs2uint64(tv);
   if( lastrun == 0 ) {
      /* set up initial state */
      lastrun = last_hb_reply = fulltime;
   }else
   if( fulltime > lastrun + ((gulm_config.heartbeat_rate*2)/3) - 1 ) {
      log_msg(lgm_Heartbeat, 
            "Sending heartbeat to Core Master at %"PRIu64", last was %"PRIu64
            "\n",
            fulltime, lastrun);
      lastrun = fulltime;
      if( poller.MasterIDX == -1 ) {
         log_msg(lgm_Network2, "No Master to send heartbeats to\n");
         return -1;
      }
      do {
         xdr_enc_t *enc = poller.enc[poller.MasterIDX];
         if((err=xdr_enc_uint32(enc, gulm_core_beat_req)) !=0 ) break;
         if((err=xdr_enc_string(enc, myName)) !=0 ) break;
         if((err=xdr_enc_flush(enc)) !=0 ) break;
      }while(0);

      if( err<0) {
         log_msg(lgm_Network2, "Failed to send heartbeat\n");
         return -1;
      }
   }
   /* Check to see if we're getting timely replies from the Master. */
   if( fulltime > last_hb_reply + gulm_config.heartbeat_rate ) {
      master_missed++;
      last_hb_reply = fulltime; /* gotta actually wait delay time before
                                  * we can say we missed the next heartbeat
                                  */
      log_msg(lgm_Network,
            "Failed to receive a timely heartbeat reply from Master. "
            "(t:%"PRIu64" mb:%d)\n", fulltime, master_missed);

      if( master_missed > gulm_config.allowed_misses ) {
         /* Master must be dead.
          * cute trick: since we only get these from the MasterFD, we
          * return -1 here, and thing will properly behave as if the
          * Master died.
          * */
         master_missed = 0;
         last_hb_reply = 0;
         log_msg(lgm_Network2, "Failed to get timely heartbeat replies\n");
         return -1;
      }
   }
   return 0;
}

/**
 * self_beat - 
 *
 * Master calls this to keep itself alive.
 */
void self_beat(void)
{
   static uint64_t lastrun=0;

   if( lastrun == 0 ) {
      lastrun = tvs2uint64(NOW);
   }else
   if( tvs2uint64(NOW) > lastrun + ((gulm_config.heartbeat_rate*2)/3) ) {
      lastrun = tvs2uint64(NOW);
      beat_node(myName, -1);
   }
}

/**
 * do_logout - 
 * 
 * Returns: int
 */
int do_logout(void)
{
   /* tell children we're gonning byebye */
   send_mbrshp_to_children(myName, gio_Mbr_Logged_out);

   if( I_am_the == gio_Mbr_ama_Slave || poller.MasterIDX != -1) {
      xdr_enc_t *enc;
      enc = poller.enc[poller.MasterIDX];
      xdr_enc_uint32(enc, gulm_core_logout_req);
      xdr_enc_string(enc, myName);
      xdr_enc_uint8(enc, I_am_the);
      xdr_enc_flush(enc);
      return 0;
   }else
   if( I_am_the == gio_Mbr_ama_Master ) {
      log_msg(lgm_Always, "Master Node Is Logging Out NOW!\n");
      send_mbrshp_to_slaves(myName, gio_Mbr_Logged_out);
   }else
   if( I_am_the == gio_Mbr_ama_Arbitrating ) {
      log_msg(lgm_Always, "Arbitrating Node Is Logging Out NOW!\n");
      send_mbrshp_to_slaves(myName, gio_Mbr_Logged_out);
   }
   return 0;
}

/**
 * determin_MyRank - 
 * 
 * figure out my rank in the server list
 *
 * Returns: int
 */
int determin_MyRank(void)
{
   int i;
   LLi_t *tmp;
   ip_name_t *in;

   for(tmp = LLi_next(&gulm_config.node_list), i = 0;
       NULL != LLi_data(tmp);
       tmp = LLi_next(tmp), i++) {
      in = LLi_data(tmp);
      if( IN6_ARE_ADDR_EQUAL(in->ip.s6_addr32, myIP.s6_addr32) ) {
         MyRank = i;
         break;
      }
   }
   return MyRank;
}

/**
 * scan_for_dead_children - 
 */
void scan_for_dead_children(void)
{
   pid_t pid;
   int status = 0;

   while( (pid=waitpid(-1, &status, WNOHANG)) > 0 ) {/* zombie scrubber. */
      /* scan for Stomith actions that have finished. */
      check_for_zombied_stomiths(pid, status);
   }
}

/**
 * work_loop - 
 * 
 */
void work_loop(void)
{
   int idx, cnt;

   gettimeofday(&Started_at, NULL);
   gettimeofday(&NOW, NULL);
   determin_MyRank();
   login_setup();
   add_node(myName, &myIP); /* for the sake of our services. */
   Mark_Loggedin(myName);
   set_nodes_mode(myName, I_am_the);

   while( running ) {

      /* the waitpid won't block, so depending on what adam sees with
       * Attachment #158 on Bug #1288  I want to try removing the SIGCHLD
       * completely.  And just whipover the waitpid everysingle time.
       */
      if( SIGCHLD_TRIPPED ) scan_for_dead_children();

      if( I_am_the == gio_Mbr_ama_Slave) {
         if( send_heartbeat() != 0) {
            /* if we lost the tcp socket to the master, we need to do a
             * rebuild.  Note that it is possible that we will reconnect to
             * the same node doing the master work.  But because of the way
             * tcp is designed, we need to destroy and rebuild the socket.
             */
            if( MasterIN != NULL )
               send_mbrshp_to_children(MasterIN->name, gio_Mbr_Expired);
            close_by_idx(poller.MasterIDX);
         }
      }else
      if( I_am_the == gio_Mbr_ama_Master ||
          I_am_the == gio_Mbr_ama_Arbitrating ) {
         /* update our own heartbeat.  */
         self_beat();
         /* make sure eveyone is sending us heartbeats. */
         check_beats();
      }

      if( (cnt = poll(poller.polls, poller.maxi +1, 2)) <= 0) {
         if( cnt < 0 && errno != EINTR )
            log_err("poll error: %s\n",strerror(errno));
         if(!running) return;
         errno = 0; /* reset this. */
      }
      gettimeofday(&NOW, NULL);
      if( ( I_am_the == gio_Mbr_ama_Pending ||
            I_am_the == gio_Mbr_ama_Arbitrating ) &&
          ! Login_state.testing &&
          Login_state.lasttry + gulm_config.master_scan_delay <tvs2uint64(NOW)){
         if( master_probe_top() != 0 )
            master_probe_bottom();
      }
      for( idx=0; idx <= poller.maxi ; idx++) {
         if (poller.polls[idx].revents & POLLNVAL ) {
            log_err("POLLNVAL on idx:%d fd:%d name:%s\n", idx,
                  poller.polls[idx].fd, poller.ipn[idx].name);
            close_by_idx(idx);
         }
         if( poller.polls[idx].revents & POLLOUT ) {
            if( poller.state[idx] == poll_Connecting ) {
               master_probe_connected(idx);
            }
         }
         if( poller.polls[idx].revents & POLLIN ) {
            if( poller.polls[idx].fd == poller.listenFD ) {
               accept_connection();
            }else{
               if( I_am_the == gio_Mbr_ama_Master ||
                   I_am_the == gio_Mbr_ama_Slave ) {
                  /* should I check to make sure only poll_Open and
                   * poll_Accepting states at this point?
                   */
                  recv_some_data(idx);
               }else
               if( I_am_the == gio_Mbr_ama_Arbitrating ||
                   I_am_the == gio_Mbr_ama_Pending ) {
                  if( poller.state[idx] == poll_Accepting ||
                      poller.state[idx] == poll_Open ) {
                     recv_some_data(idx);
                     /* For opened, or new connections, Arbit is the same
                      * as Master or Slave.
                      * It is just the Trying connection that need to be
                      * delt with differently.
                      */
                  }else
                  if( poller.state[idx] == poll_Trying ) {
                     master_probe_middle(idx);
                  }
               }else{
                  /* I_am_the is in a weird state. */
                  die(ExitGulm_Assertion,
                        "I_am_the(%#x) is in a weird state\n", I_am_the);
               }
            }
         }
         if( poller.polls[idx].revents & POLLHUP ) {
            if( poller.state[idx] == poll_Connecting ) {
               log_msg(lgm_LoginLoops, "No lock_gulmd listening at "
                     "%s idx:%d fd:%d\n",
                     print_ipname(&poller.ipn[idx]),
                     idx, poller.polls[idx].fd);
            }else{
               log_err("POLLHUP on idx:%d fd:%d name:%s\n", idx,
                     poller.polls[idx].fd, poller.ipn[idx].name);
            }
            if( ( I_am_the == gio_Mbr_ama_Pending ||
                  I_am_the == gio_Mbr_ama_Arbitrating ) &&
                ( poller.state[idx] == poll_Trying ||
                  poller.state[idx] == poll_Connecting
                 ) ) master_probe_bottom();
            close_by_idx(idx);
         }
         if (poller.polls[idx].revents & POLLERR ) {
            if( poller.state[idx] == poll_Connecting ) {
               /* do I know what errors? */
               log_msg(lgm_LoginLoops, "Errors trying to get to Master at "
                     "%s idx:%d fd:%d\n",
                     print_ipname(&poller.ipn[idx]),
                     idx, poller.polls[idx].fd);
            }else{
               log_err("An error on poller idx:%d fd:%d name:%s\n", idx,
                     poller.polls[idx].fd, poller.ipn[idx].name);
            }
            if( ( I_am_the == gio_Mbr_ama_Pending ||
                  I_am_the == gio_Mbr_ama_Arbitrating ) &&
                ( poller.state[idx] == poll_Trying ||
                  poller.state[idx] == poll_Connecting
                 ) ) master_probe_bottom();
            close_by_idx(idx);
         }

      /* check for timed out pollers. */
         if( poller.times[idx] != 0 &&
             poller.times[idx] + gulm_config.new_con_timeout < tvs2uint64(NOW)){
            log_msg(lgm_Network, "Timeout (%"PRIu64") on fd:%d (%s)\n",
                  gulm_config.new_con_timeout, poller.polls[idx].fd, 
                  print_ipname(&poller.ipn[idx]));
            if( ( I_am_the == gio_Mbr_ama_Pending ||
                  I_am_the == gio_Mbr_ama_Arbitrating ) &&
                ( poller.state[idx] == poll_Trying ||
                  poller.state[idx] == poll_Connecting
                 ) )
               master_probe_bottom(); /* time out trying to connect to
                                       * this Master.
                                       */
            close_by_idx(idx); /* or something like this. */
         }

         if(!running) return;
      }/*for( i=0; i <= poller.maxi ; i++) */
   }/* while() */
}

/* vim: set ai cin et sw=3 ts=3 : */

