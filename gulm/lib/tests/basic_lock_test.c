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
#define TRUE (1)
#define FALSE (0)

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

/**
 * lkeytohex - 
 * @key: 
 * @keylen: 
 * 
 * Returns: char
 */
char *lkeytohex(uint8_t *key, uint8_t keylen)
{
   static uint8_t buf[1024];
   int i;
   sprintf(buf, "0x");
   for(i=0; i < keylen && i < 510 ; i++)
      sprintf(&buf[(i*2)+2], "%02x", (key[i])&0xff );

   return buf;
}
char *lvbtohex(uint8_t *lvb, uint8_t lvblen)
{
   static uint8_t buf[1024];
   int i;
   sprintf(buf, "0x");
   for(i=0; i < lvblen && i < 510 ; i++)
      sprintf(&buf[(i*2)+2], "%02x", (lvb[i])&0xff );

   return buf;
}
/*****************************************************************************/
void act_logout_lock(gulm_interface_p hookup, void *misc)
{
   int err;
   if((err=lg_lock_logout(hookup))!=0)
      die("Error sending lock logout. %d\n", err);
}
void act_logout_core(gulm_interface_p hookup, void *misc)
{
   int err;
   if((err=lg_core_logout(hookup))!=0)
      die("Error sending core logout. %d\n", err);
}
void act_lock(gulm_interface_p hookup, void *misc)
{
   int err;
   if((err=lg_lock_state_req(hookup, "justatest", 10, 0, 0, 0,
               lg_lock_state_Exclusive, 0, "FOO!", 5))!=0)
      die("Error sending lock request. %d\n", err);
}
void act_locksh(gulm_interface_p hookup, void *misc)
{
   int err;
   if((err=lg_lock_state_req(hookup, "justatest", 10, 0, 0, 0,
               lg_lock_state_Shared, 0, "BAR!", 5))!=0)
      die("Error sending lock request. %d\n", err);
}
void act_unlock(gulm_interface_p hookup, void *misc)
{
   int err;
   if((err=lg_lock_state_req(hookup, "justatest", 10, 0, 0, 0,
               lg_lock_state_Unlock, 0, NULL, 0))!=0)
      die("Error sending unlock request. %d\n", err);
}
void act_hold(gulm_interface_p hookup, void *misc)
{
   int err;
   if((err=lg_lock_action_req(hookup, "justatest", 10, 0,
               lg_lock_act_HoldLVB, NULL, 0))!=0)
      die("Error sending hold request. %d\n", err);
}
void act_unhold(gulm_interface_p hookup, void *misc)
{
   int err;
   if((err=lg_lock_action_req(hookup, "justatest", 10, 0,
               lg_lock_act_UnHoldLVB, NULL, 0))!=0)
      die("Error sending unhold request. %d\n", err);
}

/*****************************************************************************/
struct workstep_s {
   /* what to do. */
   void (*action)(gulm_interface_p hookup, void *misc);
   /* where are we? */
   enum {
      ws_ready,
      ws_pending,
      ws_complete
   } state;
   /* what we expect. */
};

int All_Done = FALSE;
unsigned int Current_Action = 0;
struct workstep_s Actions[] = {
   { act_hold, ws_ready },
   { act_lock, ws_ready },
   { act_locksh, ws_ready },
   { act_unlock, ws_ready },
   { act_unhold, ws_ready },
   { act_logout_lock, ws_ready },
   { act_logout_core, ws_ready }
};

void do_action(gulm_interface_p hookup, void *misc)
{
   unsigned int maxact = sizeof(Actions)/sizeof(Actions[0]);

   if( Current_Action >= maxact ) { All_Done = TRUE; return; }

   if( Actions[Current_Action].state == ws_ready ) {
      Actions[Current_Action].action(hookup, misc);
      Actions[Current_Action].state = ws_pending;
   }else
   if( Actions[Current_Action].state == ws_pending ) {
      /* nop. */
   }else
   if( Actions[Current_Action].state == ws_complete ) {
      Current_Action ++;
      /* call into self. */
      do_action(hookup, misc);
   }
}

void complete_action(void)
{
   unsigned int maxact = sizeof(Actions)/sizeof(Actions[0]);

   if( Current_Action >= maxact ) { All_Done = TRUE; return; }
   Actions[Current_Action].state = ws_complete;
}

/*****************************************************************************/
int cb_core_login_reply(void *misc, uint64_t gen, uint32_t err, uint32_t rank, uint8_t corestate)
{

   printf("Got a Core Login reply.  gen:%lld err:%d rank:%d corestate:%d\n",
         gen, err, rank, corestate);
   return 0;
}

int cb_core_logout_reply(void *misc)
{
   printf("Got Core logout reply\n");
   complete_action();
   return 0;
}

int cb_nodechange(void *misc, char *nodename,
                 struct in6_addr *nodeip, uint8_t nodestate)
{
   printf("Got Nodechange, node:%s ip:%#x state:%#x\n",
         nodename, nodeip, nodestate);
   return 0;
}

int cb_core_error(void *misc, uint32_t err)
{
   die("Got error reply %d from core\n", err);
   return 0;
}


/*
 * The only thing we need from core is basic login.
 */
lg_core_callbacks_t gulm_core_cbs = {
   login_reply:  cb_core_login_reply,
   logout_reply: cb_core_logout_reply,
   nodelist:     NULL,
   statechange:  NULL,
   nodechange:   cb_nodechange,
   service_list: NULL,
   error:        cb_core_error
};
/*****************************************************************************/
int cb_lock_login_reply(void *misc, uint32_t error, uint8_t which)
{
   printf("Got lock login reply: err:%d which:%#x\n", error, which);
   return 0;
}

int cb_lock_logout_reply(void *misc)
{
   printf("Got Lock logout reply\n");
   complete_action();
   return 0;
}

int cb_lock_drop_all(void *misc)
{
   printf("Got drop all request\n");
   return 0;
}

int cb_lock_state(void *misc, uint8_t *key, uint16_t keylen,
                 uint64_t subid, uint64_t start, uint64_t stop,
                 uint8_t state, uint32_t flags, uint32_t error,  
                 uint8_t *LVB, uint16_t LVBlen)
{
   if(LVB != NULL ) {
   printf("Got lock reply: %s\n"
          "         state: %#x\n"
          "         flags: %#x\n"
          "         error: %d\n"
          "        lvblen: %d\n"
          "           lvb: %s\n",
          lkeytohex(key, keylen),
          state, flags, error, LVBlen,
          lvbtohex(LVB, LVBlen)
         );
   }else{
   printf("Got lock reply: %s\n"
          "         state: %#x\n"
          "         flags: %#x\n"
          "         error: %d\n"
          "           lvb: NULL\n",
          lkeytohex(key, keylen),
          state, flags, error
         );
   }
   complete_action();
   return 0;
}

int cb_lock_action(void *misc, uint8_t *key, uint16_t keylen, uint64_t subid,
                  uint8_t action, uint32_t error)
{
   printf("Got lock reply: %s\n"
          "        action: %#x\n"
          "         error: %d\n",
          lkeytohex(key, keylen),
          action, error
         );
   complete_action();
   return 0;
}

int cb_lock_drop_req(void *misc, uint8_t *key, uint16_t keylen, uint64_t subid,
      uint8_t state)
{
   printf("Lock servers wants us to drop lock %s into state %#x\n",
         lkeytohex(key, keylen), state);
   return 0;
}

int cb_lock_error(void *misc, uint32_t err)
{
   die("Got error reply %d from lock\n", err);
   return 0;
}

lg_lockspace_callbacks_t gulm_lock_cbs = {
   login_reply:   cb_lock_login_reply,
   logout_reply:  cb_lock_logout_reply,
   lock_state:    cb_lock_state,
   lock_action:   cb_lock_action,
   drop_lock_req: cb_lock_drop_req,
   drop_all:      cb_lock_drop_all,
   error:         cb_lock_error
};
/*****************************************************************************/

int main(int argc, char **argv)
{
   int err, cnt;
   gulm_interface_p hookup;
   struct pollfd pls[2];

   setupsignals();

   printf("Starting TestBox For Lock\n");

   if( (err = lg_initialize(&hookup, NULL, "TestBox For Lock")) != 0 ) {
      die("Failed to lg_initialize() err:%d\n",err);
   }

   if((err = lg_core_login(hookup, FALSE)) != 0 ) {
      die("Failed to send login request to core. err %d\n", err);
   }
   if((err = lg_lock_login(hookup, "TEST")) != 0 ) {
      die("Failed to send login request to core. err %d\n", err);
   }

   pls[0].fd = lg_core_selector(hookup);
   pls[0].events = POLLIN;
   pls[1].fd = lg_lock_selector(hookup);
   pls[1].events = POLLIN;

   while(!All_Done) {
      if( (cnt = poll(pls, 2, 1000)) <= 0 ) {
         fprintf(stderr, "poll error: %d\n", cnt);
      }
      if( pls[0].revents & POLLIN ) {
         if((err = lg_core_handle_messages(hookup, &gulm_core_cbs, NULL))!= 0){
            die("Bad return from core handle messages err %d\n", err);
         }
      }
      if( pls[1].revents & POLLIN ) {
         if((err = lg_lock_handle_messages(hookup, &gulm_lock_cbs, NULL))!= 0){
            die("Bad return from lock handle messages err %d\n", err);
         }
      }


      do_action(hookup, NULL);
   }

   lg_release(hookup);
   return 0;
}

