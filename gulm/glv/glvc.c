/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
**
*******************************************************************************
******************************************************************************/
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <netdb.h>

#include "libgulm.h"

#define TRUE (1)
#define FALSE (0)

#define die(msg, args...) {fprintf(stderr, msg, ## args);exit(1);}
#define perr(msg, args...) fprintf(stderr, msg, ## args)
#define verb(level, msg, args...) {if(verbing >= level) {printf(msg, ## args);}}
/*************************************************************************/
/* sometimes, globals are just easier. */
gulm_interface_p hookup;
int masterSK = -1;
int running = TRUE;
int verbing = 0;

/*************************************************************************/

int glv_core_login_reply(void *misc, uint64_t gen, uint32_t error,
      uint32_t rank, uint8_t corestate)
{
   int err;

   if( error != 0 )
      die("Failed to loginto core %d\n", error);

   if( (err=lg_lock_login(hookup, "glv ")) != 0 )
      die("Failed to call lock login. %d\n", err);

   return 0;
}

int glv_core_logout_reply(void *misc)
{
   return 0;
}

int glv_core_error(void *misc, uint32_t err)
{
   die("Weird error in core %d\n", err);
   return 0;
}

static lg_core_callbacks_t core_ops = {
login_reply: glv_core_login_reply,
logout_reply: glv_core_logout_reply,
error: glv_core_error
};
/*************************************************************************/
int glv_lock_login_reply(void *misc, uint32_t error, uint8_t which)
{
   if( error != 0 )
      die("Failed to loginto lock %d\n");
   return 0;
}

int glv_lock_state(void *misc, uint8_t *key, uint16_t keylen,
      uint8_t state, uint32_t flags, uint32_t error,  
                    uint8_t *LVB, uint16_t LVBlen)
{
   char buffy[160];
   int actual;
   /* kinda a cheet here. */
   if( strlen(LVB) == 0 ) LVBlen = 0;

   actual = snprintf(buffy, 160, "lrpl %s %d %d %d %s\n", key, state, flags,
         error, LVBlen==0?"nolvb":(char*)LVB);
   verb(3, "Sending to glvd: %s", buffy);
   send(masterSK, buffy, actual, 0);
   return 0;
}

int glv_lock_action(void *misc, uint8_t *key, uint16_t keylen, uint8_t action, 
                     uint32_t error)
{
   char buffy[160];
   int actual;
   actual = snprintf(buffy, 160, "arpl %s %d %d\n", key, action, error);
   verb(3, "Sending to glvd: %s", buffy);
   send(masterSK, buffy, actual, 0);
   return 0;
}

int glv_lock_drop_lock_req(void *misc, uint8_t *key, uint16_t keylen,
      uint8_t state)
{
   char buffy[160];
   int actual;
   actual = snprintf(buffy, 160, "drop %s %d\n", key, state);
   verb(3, "Sending to glvd: %s", buffy);
   send(masterSK, buffy, actual, 0);
   return 0;
}

int glv_lock_error(void *misc, uint32_t err)
{
   die("Weird error in lock %d\n", err);
   return 0;
}

static lg_lockspace_callbacks_t lock_ops = {
login_reply: glv_lock_login_reply,
lock_state: glv_lock_state,
lock_action: glv_lock_action,
drop_lock_req: glv_lock_drop_lock_req,
error: glv_lock_error
};
/*************************************************************************/

void parse_action(int sk)
{
   char buffy[160], *key=NULL, *lvb=NULL;
   int cnt, state, flags;
   
   if( (cnt=recv(sk, buffy, 160, 0)) <0)
      die("read failed. %d\n");
   if(cnt == 0 ) {
      verb(1,"EOF from glvd\n");
      running = FALSE;
      return;
   }
   buffy[cnt-1] = '\0';
   verb(3, "Got from glvd: %s\n", buffy);

   if(sscanf(buffy, "lock %as %d %d %as", &key, &state, &flags, &lvb) == 4) {
      if( strcmp(lvb, "nolvb") == 0 ) {free(lvb); lvb=NULL;}
      verb(3, "Matched lock.\n");
      lg_lock_state_req(hookup, key, strlen(key)+1, state, flags, lvb,
            lvb==NULL?0:(strlen(lvb)+1) );
   }else
   if(sscanf(buffy, "action %as %d %as", &key, &state, &lvb) == 3) {
      if( strcmp(lvb, "nolvb") == 0 ) {free(lvb); lvb=NULL;}
      verb(3, "Matched action.\n");
      lg_lock_action_req(hookup, key, strlen(key)+1, state, lvb,
            lvb==NULL?0:(strlen(lvb)+1) );
   }else
   if(sscanf(buffy, "cancel %as", &key) == 1) {
      verb(3, "Matched cancel.\n");
      lg_lock_cancel_req(hookup, key, strlen(key)+1);
   }else
   if(sscanf(buffy, "dropexp %as %as", &lvb, &key) == 2) {
      verb(3, "Matched dropexp.\n");
      /* lvb here is really node name */
      lg_lock_drop_exp(hookup, lvb, key, strlen(key)+1);
   }else
   if(strcmp(buffy, "GOODBYE") == 0) {
      running = FALSE;
   }else
   {
      verb(3, "Nothing matched\n");
   }

   if( key != NULL ) free(key);
   if( lvb != NULL ) free(lvb);
}


/*************************************************************************/
/**
 * connect_to_serv - 
 * @h: 
 * @p: 
 * 
 * 
 * Returns: int
 */
int connect_to_serv(char *h, char *p)
{
   struct sockaddr_in adr;
   unsigned short port=20016;/* default */
   struct hostent *hent;
   int sock, trueval=TRUE;

   if( p != NULL ) port = atoi(p);

   hent = gethostbyname(h);
   if(!hent) return -1;
   if(! hent->h_addr_list ) return -1;

   adr.sin_family = AF_INET;
   adr.sin_addr.s_addr = *((int*)(hent->h_addr_list[0]));/*eeewww*/
   adr.sin_port = htons( port );

   if( (sock = socket(AF_INET, SOCK_STREAM, 0))<0)
      die("Failed to get sockmonkey\n");
   if( connect(sock, (struct sockaddr*)&adr, sizeof(struct sockaddr_in))<0)
      die("Failed to attach sockmonkey\n");

   return sock;
}

/**
 * login - 
 * @sk: 
 * @name: 
 * 
 * 
 * Returns: void
 */
void login(int sk, char *name)
{
   char buffy[80];
   int actual;

   actual = snprintf(buffy, 80, "Hello %s\n", name);
   verb(3, "Sending to glvd: %s", buffy);
   send(sk, buffy, actual, 0);

   if( (actual=recv(sk, buffy, 80, 0)) <0)
      die("Failed to recv login reply %d\n", errno);
   buffy[actual-1] = '\0';
   verb(3, "Got from glvd: %s\n", buffy);

   if( strcmp(buffy, "HI") != 0 )
      die("Got weird login reply: %s\n", buffy);
}

/**
 * usage - 
 * @oid: 
 * 
 * 
 * Returns: void
 */
void usage(void)
{
   char *strings[] = {
   "Usage:\n",
   "glvc <server> [<port>]\n",
   "\n"
   };
   int i;
   for(i=0; strings[i] != NULL; i++)
      printf("%s", strings[i]);
   exit(0);
}

/**
 * main - 
 * @argc: 
 * @argv: 
 * 
 * 
 * Returns: int
 */
int main(int argc, char **argv)
{
   char hostname[64];
   struct pollfd polls[3]; /* server, core, lock */
   int err;
   
   if( argc < 2 || argc > 3 )
      usage();

   if(gethostname(hostname, 64) <0) strcpy(hostname,"NOPNOP");

   if( lg_initialize(&hookup, "", "glv") != 0 )
      die("Failed to initialize libgulm\b");

   /* daemonize (cuz ssh is a turd otherwise) */
   errno=0;
   if(fork() == 0) exit(0);
   if(errno != 0) exit(1);

   /* connect to server */
   polls[0].fd = masterSK = connect_to_serv(argv[1], argv[2]);
   polls[0].events = POLLIN;
   login(polls[0].fd, hostname);

   /* connect to gulm core */
     /* connect to gulm lock */
   if( lg_core_login(hookup, FALSE) != 0 )
      die("Failed to call login to core\n");

   lg_core_handle_messages(hookup, &core_ops, NULL);


   polls[1].fd = lg_core_selector(hookup);
   polls[1].events = POLLIN;
   polls[2].fd = lg_lock_selector(hookup);
   polls[2].events = POLLIN;

   /*poll*/
   while(running) {
      if( (err = poll(polls, 3, -1)) <0) {
         perr("poll err %d\n", errno);
      }
      if( polls[0].revents & POLLHUP ) {
         verb(1, "Lost glvd\n");
         break;
      }
      if( polls[0].revents & POLLIN ) {
         parse_action(polls[0].fd);
      }
      if( polls[1].revents & POLLIN ) {
         lg_core_handle_messages(hookup, &core_ops, NULL);
      }
      if( polls[2].revents & POLLIN ) {
         lg_lock_handle_messages(hookup, &lock_ops, NULL);
      }
   }

   close(polls[0].fd);

   lg_lock_logout(hookup);
   lg_lock_handle_messages(hookup, &lock_ops, NULL);
   lg_core_logout(hookup);
   lg_core_handle_messages(hookup, &core_ops, NULL);
   lg_release(hookup);
}
/* vim: set ai cin et sw=3 ts=3 : */
