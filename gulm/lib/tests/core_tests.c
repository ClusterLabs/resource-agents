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

#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <execinfo.h>
#include <errno.h>
#include <string.h>
#include <sys/poll.h>

#include "libgulm.h"

#define die(fmt, arg...) { fprintf(stderr, fmt , ## arg ); exit(1); }

/**
 * sigact_segv - 
 * @sig: 
 * 
 * try to get a backtrace before we puke out.
 * This may not always work, but since I cannot get daemons to drop core
 * files, trying this is better than nothing.
 */
static void sigact_segv(int sig)
{
   struct sigaction act;
   void *array[200];
   size_t size,i;
   char **strings;

   size = backtrace(array, 200);
   strings = backtrace_symbols(array, size);
      fprintf(stderr, "BACKTRACE\n");
   for(i=0;i<size; i++)
      fprintf(stderr, " %s\n", strings[i]);
   free(strings);

   memset(&act,0,sizeof(act));
   act.sa_handler = SIG_DFL;
   sigaction(SIGSEGV, &act, NULL);
   raise(SIGSEGV);
}

/**
 * setupsignals - set how we respond to signals.
 */
static void setupsignals(void)
{
   struct sigaction act;

   memset(&act,0,sizeof(act));
   act.sa_handler = sigact_segv;
   if( sigaction(SIGSEGV, &act, NULL) <0)
      die("Failed to install signal SIGSEGV handler: %s\n",strerror(errno));

}
/*****************************************************************************/
int cb_login_reply(void *misc, uint64_t gen, uint32_t err, uint32_t rank, uint8_t corestate)
{

   printf("Got a Login reply.  gen:%lld err:%d rank:%d corestate:%d\n",
         gen, err, rank, corestate);

   return 0;
}

int cb_logout_reply(void *misc)
{

   printf("Got logout reply\n");

   return 0;
}

int cb_nodelist(void *misc, lglcb_t type, char *name,
               struct in6_addr *ip, uint8_t state)
{
   if( lglcb_start == type ) {
      printf("Got Nodelist, start\n");
   }else
   if( lglcb_item == type ) {
      printf("Got nodelist, item: %s, %#x, %#x\n", name, ip, state);
   }else
   if( lglcb_stop == type ) {
      printf("Got Nodelist, stop\n");
   }else
   {
      printf("Unknown lglcb_t %#x\n", type);
   }
   return 0;
}

int cb_statechange(void *misc, uint8_t corestate, uint8_t quorate,
                  struct in6_addr *masterip, char *mastername)
{
   printf("Got statechange  corestate:%#x quorate:%s masterip:%#x mastername:%s\n",
         corestate, quorate?"true":"false", masterip, mastername);
   return 0;
}

int cb_nodechange(void *misc, char *nodename,
                 struct in6_addr *nodeip, uint8_t nodestate)
{
   printf("Got Nodechange, node:%s ip:%#x state:%#x\n",
         nodename, nodeip, nodestate);
   return 0;
}

int cb_service_list(void *misc, lglcb_t type, char *service)
{
   if( lglcb_start == type ) {
      printf("Got service_list, start\n");
   }else
   if( lglcb_item == type ) {
      printf("Got service_list, item: %s\n", service);
   }else
   if( lglcb_stop == type ) {
      printf("Got service_list, stop\n");
   }else
   {
      printf("Unknown lglcb_t %#x\n", type);
   }
   return 0;
}

int cb_error(void *misc, uint32_t err)
{
   printf("Got error %d\n", err);
   return 0;
}


int main(int argc, char **argv)
{
   int err;
   gulm_interface_p hookup;
   lg_core_callbacks_t lcb;
#if 0
   struct pollfd pls[2];
#endif
   setupsignals();

   printf("Starting TestBox For Core\n");

   if( (err = lg_initialize(&hookup, NULL, "TestBox For Core")) != 0 ) {
      die("Failed to lg_initialize() err:%d\n",err);
   }

   lcb.login_reply = cb_login_reply;
   lcb.logout_reply = cb_logout_reply;
   lcb.nodelist = cb_nodelist;
   lcb.statechange = cb_statechange;
   lcb.nodechange = cb_nodechange;
   lcb.service_list = cb_service_list;
   lcb.error = cb_error;


   if((err = lg_core_login(hookup, 0)) != 0 ) {
      die("Failed to send login request to core. err %d\n", err);
   }

   if((err = lg_core_handle_messages(hookup, &lcb, NULL)) != 0 ) {
      die("Bad return from handle messages err %d\n", err);
   }
   
   sleep(2); /* no real reason for this. */

   if((err = lg_core_servicelist(hookup)) != 0 ) {
      die("Failed to send login request to core. err %d\n", err);
   }

   if((err = lg_core_handle_messages(hookup, &lcb, NULL)) != 0 ) {
      die("Bad return from handle messages err %d\n", err);
   }
   
   sleep(2); /* no real reason for this. */

   if((err = lg_core_nodelist(hookup)) != 0 ) {
      die("Failed to send login request to core. err %d\n", err);
   }

   if((err = lg_core_handle_messages(hookup, &lcb, NULL)) != 0 ) {
      die("Bad return from handle messages err %d\n", err);
   }
   
   sleep(2); /* no real reason for this. */

   if((err = lg_core_corestate(hookup)) != 0 ) {
      die("Failed to send login request to core. err %d\n", err);
   }

   if((err = lg_core_handle_messages(hookup, &lcb, NULL)) != 0 ) {
      die("Bad return from handle messages err %d\n", err);
   }
   
   sleep(2); /* no real reason for this. */



   if((err = lg_core_logout(hookup)) != 0 ) {
      die("Failed to send login request to core. err %d\n", err);
   }

   if((err = lg_core_handle_messages(hookup, &lcb, NULL)) != 0 ) {
      die("Bad return from handle messages err %d\n", err);
   }

   lg_release(hookup);
   return 0;
}
