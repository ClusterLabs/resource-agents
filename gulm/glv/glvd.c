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

#include "glvd.h"

#define TRUE (1)
#define FALSE (0)

#define die(msg, args...) {fprintf(stderr, msg, ## args);exit(1);}
#define perr(msg, args...) fprintf(stderr, msg, ## args)
#define verb(level, msg, args...) {if(verbing >= level) {printf(msg, ## args);}}
/*************************************************************************/
/* some globals. */
char hostname[64]="\0";
uint16_t serv_port = 20016;
int test = FALSE;
int verbing = 0;

char *TestFile=NULL;
struct glv_testfile *Tests=NULL;

char **Nodes=NULL;
int *NodeSKs=NULL;
int NodeCount=0;

struct pollfd *polls;

/*************************************************************************/
/*************************************************************************/

int nodeidx_from_fd(int fd)
{
   int i;
   for(i=0;i<NodeCount;i++) {
      if( NodeSKs[i] == fd ) return i;
   }
   return -1;
}

/**
 * verify_reaction - 
 * @in: 
 * @reactions: 
 */
void verify_reaction(struct glv_reaction *in, struct glv_test *running)
{
   int allmatched = 1, found = 0;
   struct glv_reaction *tmp;
   for(tmp = running->react; tmp != NULL; tmp = tmp->next) {
      if( tmp->matched == 0 &&
          in->nodeidx == tmp->nodeidx &&
          in->react == tmp->react &&
          strcmp(in->key, tmp->key) == 0 ) {
         /* ok, does the rest ok? */

         if( in->state != tmp->state ) {
            /* print incomming */
            die("Expected state(%s) is not what we got(%s) on line %d\n",
                  statestrings(tmp->state), statestrings(in->state), tmp->line);
         }
         if( in->flags != tmp->flags ) {
            char temp[60];
            strcpy(temp, flagsstr(in->flags));
            die("Expected flags(%s %#x) is not what we got(%s %#x) on line %d\n",
                  flagsstr(tmp->flags), tmp->flags,
                  temp, in->flags,
                  tmp->line);
         }
         if( in->error != tmp->error ) {
            die("Expected error(%s) is not what we got(%s) on line %d\n",
                  errstring(tmp->error), errstring(in->error), tmp->line);
         }
         if( in->lvb == NULL && tmp->lvb == NULL ) {
            /* both NULL, things are ok. so a nop. */
         }else
         if( in->lvb == NULL && tmp->lvb != NULL ) {
            /* die */
            die("Expected lvb(%s) but got nothing on line %d\n",
                  tmp->lvb, tmp->line);
         }else
         if( in->lvb != NULL && tmp->lvb == NULL ) {
            /* die */
            die("Expected no lvb, but got(%s) on line %d\n",
                  in->lvb, tmp->line);
         }else
         if( strcmp(in->lvb, tmp->lvb) != 0 ) {
            /* die */
            die("Expected lvb(%s) is not what we got(%s) on line %d\n",
                  tmp->lvb, in->lvb, tmp->line);
         }

         tmp->matched = 1;
         found = 1;
         verb(1, "Matched reaction.\n");
      }
      if( tmp->matched == 0 ) allmatched = 0;
   }

   running->allmatched = allmatched;

   if( found ) return;

   /* print incomming */
   print_reaction(stderr, in);
   die("Failed to find matching reaction in test on line %d\n",
         running->line);
}

/**
 * check_reactions - 
 * @sk: 
 * @reactions: 
 */
void check_reactions(int sk, struct glv_test *running)
{
   char buffy[160], key[60], lvb[60];
   struct glv_reaction incomming;
   int cnt;

   memset(&incomming, 0, sizeof(struct glv_reaction));
   incomming.nodeidx = nodeidx_from_fd(sk);
   if( incomming.nodeidx < 0 ) die("Cannot find sk %d in NodeSKs!!!\n", sk);

   if( (cnt=recv(sk, buffy, 160, 0)) <0) {
      perr("Failed to read data from %s (%d)\n", Nodes[incomming.nodeidx],
            errno);
      return;
   }
   if( cnt == 0 ) {
      die("EOF from glvc[%d]\n", incomming.nodeidx);
      return;
   }
   buffy[cnt-1] = '\0';
   verb(3, "Got from glvc[%d]: %s\n", incomming.nodeidx, buffy);

   if(sscanf(buffy, "lrpl %s %d %d %d %s", 
               key, &incomming.state, &incomming.flags,
               &incomming.error, lvb) == 5) {
      if( strcmp(lvb, "nolvb") == 0 ) {
         incomming.lvb = NULL;
      }else{
         incomming.lvb = lvb;
      }
      incomming.react = glv_lrpl;
      incomming.key = key;
      verify_reaction(&incomming, running);
   }else
   if(sscanf(buffy, "arpl %s %d %d", 
               key, &incomming.state, &incomming.error) == 3 ) {
      incomming.react = glv_arpl;
      incomming.key = key;
      verify_reaction(&incomming, running);
   }else
   if(sscanf(buffy, "drop %s %d", key,
               &incomming.state) == 2 ) {
      incomming.react = glv_drop;
      incomming.key = key;
      verify_reaction(&incomming, running);
   }else
   if(strcmp(buffy, "GOODBYE" ) == 0 ) {
   }else
   {
      perr("Garbage buffer follows.\n%s\n", buffy);
   }

}

/**
 * do_action - 
 * @action: 
 */
void do_action(struct glv_action *action)
{
   char buffy[160];
   int actual;
   switch(action->action) {
      case glv_lock:
         actual = snprintf(buffy, 160, "lock %s %d %d %s\n",
               action->key, action->state, action->flags,
               action->lvb==NULL?"nolvb":action->lvb);
         verb(3, "Sending to glvc[%d] %s", action->nodeidx, buffy);
         send(NodeSKs[action->nodeidx], buffy, actual, 0);
         break;
      case glv_act:
         actual = snprintf(buffy, 160, "action %s %d %s\n",
               action->key, action->state,
               action->lvb==NULL?"nolvb":action->lvb);
         verb(3, "Sending to glvc[%d] %s", action->nodeidx, buffy);
         send(NodeSKs[action->nodeidx], buffy, actual, 0);
         break;
      case glv_cancel:
         actual = snprintf(buffy, 160, "cancel %s\n",
               action->key);
         verb(3, "Sending to glvc[%d] %s", action->nodeidx, buffy);
         send(NodeSKs[action->nodeidx], buffy, actual, 0);
         break;
      case glv_dropexp:
         actual = snprintf(buffy, 160, "dropexp %s %s\n",
               action->lvb, /* lvb here is overloaded. */
               action->key);
         verb(3, "Sending to glvc[%d] %s", action->nodeidx, buffy);
         send(NodeSKs[action->nodeidx], buffy, actual, 0);
         break;
   }
}

/**
 * run_test_run - 
 */
void run_test_run(void)
{
   struct glv_test *running;
   int err, i;
   /*
    * Basic test pass:
    *  send action to node at idx.
    *  wait at poll() for reactions
    *   ?warn if taking too long?
    *   No wait if no actions.
    *    (still should wait at poll for a second. jic)
    *  Next test block
    *
    * Any unexpected actions are reported and test stops.
    *  (glvd and glvc all stop)
    */

   for(running = Tests->tests; running != NULL; running = running->next) {
      do_action(running->action);

      if( running->react == NULL ) continue;
      /* should really run through the poll at least once to see if
       * anything shows up.
       */

      while(running->allmatched == 0) {

         if((err=poll(polls, NodeCount+3, 5000))<0) {
            perr("poll err %d\n", errno);
         }
         if(err == 0 ) {
            verb(0, "Still waiting for reactions. test on line: %d\n",
                  running->line);
         }
         for(i=1; i < NodeCount +3; i++) {
            if( polls[i].fd < 0 ) continue;
            if( polls[i].revents & POLLIN ) {
               check_reactions(polls[i].fd, running);
            }
            if( polls[i].revents & POLLHUP ) {
               die("Crap, HUP on fd %d\n", polls[i].fd);
            }
         }
      }/*while(running->allmatched == 0)*/

   }/*for(...)*/

   /* wonder if it would be smarter to liner here a moment or three, just
    * in case something comes back?
    */

   for(i=0; i < NodeCount; i++ ) {
      if( NodeSKs[i] < 0 ) continue;
      send(NodeSKs[i], "GOODBYE\n", 8, 0);
   }

   verb(0, "All tests completed.\n");

}

/*************************************************************************/
/**
 * serv_listen - start listening to a port.
 * 
 * Returns: socket file descriptor
 */
int serv_listen(void)
{
   int sk, trueint=TRUE;
   struct sockaddr_in adr;

   if( (sk = socket(AF_INET, SOCK_STREAM, 0)) <0)
      die("Failed to create socket\n");

   if( setsockopt(sk, SOL_SOCKET, SO_REUSEADDR, &trueint, sizeof(int)) <0)
      die("Failed to set sock option SO_REUSEADDR\n");

   adr.sin_family = AF_INET;
   adr.sin_addr.s_addr = INADDR_ANY;
   adr.sin_port = htons( serv_port );

   if( bind(sk, (struct sockaddr *)&adr, sizeof(struct sockaddr_in)) <0)
      die("Could not bind\n");

   if( listen(sk, 5) <0) die("Someone ripped off my ears\n");

   return sk;
}

/**
 * setup_poll - 
 */
void setup_poll(void)
{
   int i;

   polls = malloc(sizeof(struct pollfd) * NodeCount +3);
   memset(polls, 0, sizeof(struct pollfd) * NodeCount +3);
   for(i=0; i < NodeCount +3; i++) polls[i].fd = -1;

   polls[0].fd = serv_listen();
   polls[0].events = POLLIN;

}

/**
 * startup_clients - 
 *
 * this function needs work like you can't believe.
 *
 * <GVL_RSH> [<user>] <node> <GLVC_PATH> <glvd-hostname>
 */
void startup_clients(void)
{
   char buffy[160], *rsh, *user, *glvc;
   int i;
   rsh = getenv("GLV_RSH");
   if(rsh == NULL ) rsh = "ssh";
   glvc = getenv("GLVC_PATH");
   if( glvc == NULL ) glvc = "/root/bin/glvc";
   for(i=0; i < NodeCount; i++) {
      snprintf(buffy, 160, "%s %s %s %s &",
            rsh, Nodes[i], glvc, hostname);
      verb(2, "calling system on %s\n", buffy);
      system(buffy);
   }
}

/**
 * do_login - 
 * @sk: 
 * 
 * 
 * Returns: int
 */
int do_login(int sk)
{
   char ibuf[80];
   char name[64];
   int n;

   if((n = recv(sk, ibuf, 80, 0))<0){
      close(sk);
      return -1;
   }
   ibuf[n-1]='\0';
   if((n=sscanf(ibuf, "Hello %s", name)) != 1) {
      perr("Failed to scan login req.\n");
      close(sk);
      return -1;
   }
   for(n=0;n<NodeCount;n++) {
      if( strcmp(name, Nodes[n]) == 0) {
         NodeSKs[n] = sk;
         send(sk, "HI\n", 3, 0);
         return 0;
      }
   }

   perr("Someone named \"%s\" wanted to login, but that's noone we're "
         "waiting for\n", name);

   close(sk);
   return -1;
}

void wait_for_clients(void)
{
   int err, i, sk;
   int loggedin=0;
   struct sockaddr_in adr;
   /* poll with accept.
    * and check login.
    * We stick in here until all of the clients we expect are logged in.
    */

   while( loggedin != NodeCount ) {

      if( (err=poll(polls, NodeCount +3, 5000)) <0) {
         perr("poll err %d\n", errno);
      }
      if( err == 0 ) {
         verb(0,"Still waiting for clients to login.\n");
      }
      if( polls[0].revents & POLLIN ) {
         i = sizeof(struct sockaddr_in);
         if((sk = accept(polls[0].fd, (struct sockaddr*)&adr, &i)) <0)
            die("accept error: %d\n", errno);
         /* we need thier name from them.
          * then match that in the Nodes[] and save in NodeSKs[]
          * and increment loggedin
          */
         if( do_login(sk) == 0 ) {
            loggedin++;
            for(i=1; i < NodeCount +3 && polls[i].fd >= 0; i++);
            if( i >= NodeCount +3) {
               perr("Now spaces left in poll!?!?\n");
            }else{
               polls[i].fd = sk;
               polls[i].events = POLLIN;
            }
         }
      }

      /* should not actually get anything here yet. */
      for(i=1; i < NodeCount +3; i++) {
         if( polls[i].fd < 0 ) continue;
         if( polls[i].revents & POLLHUP ) {
            die("Crap, lost fd %d\n", polls[i].fd);
         }
      }

   }
   /* don't care if there are any others trying to connect to us anymore */
   close(polls[0].fd);
   polls[0].fd = -1;
   polls[0].events = 0;

   verb(0, "Ok, All clients have logged in, starting tests.\n");
}



/*************************************************************************/
void usage(void)
{
   char *strings[] = {
   "Usage:\n",
   "glvd [options] testfile <nodes>\n",
   "Options:\n",
   " --version  -V  version info\n",
   " --help     -h  this.\n",
   " --test     -t  make sure the testfile parses.\n",
   "\n"
   };
   int i;
   for(i=0; strings[i] != NULL; i++)
      printf("%s", strings[i]);
   exit(0);
}
static struct option long_options[]={
   {"version", 0, 0, 'V'},
   {"help", 0, 0, 'h'},
   {"myname", 1, 0, 'm'},
   {"port", 1, 0, 'p'},
   {"test", 0, 0, 't'}
};
int parse_cmdline(int argc, char **argv)
{
   int c;
   int option_index = 0;
   while(1) {

      c = getopt_long(argc, argv, "m:p:tVh", long_options, &option_index);
      if( c == -1 ) break;

      switch(c) {
         case 0:
            fprintf(stderr,"Bad programmer! You forgot to catch '%s' \n",
                    long_options[option_index].name);
            exit(1);
            break;
         case 'V':
            printf("This is the first version.\n");
            break;
         case 'm':
            strcpy(hostname, optarg);
            break;
         case 'p':
            serv_port = atoi(optarg);
            break;
         case 'h':
            usage();
            break;
         case 't':
            test = TRUE;
            break;
         case ':':
         case '?':
            fprintf(stderr, "Ambiguous options, see --help\n");
            exit(1);
         default:
            fprintf(stderr,"Bad programmer! You forgot to catch the %c flag\n",
                    c);
            exit(1);
            break;
      }

   }
   /* testfile and nodes should be left. */
   TestFile = strdup(argv[optind++]);
   /* rest are nodes. */
   Nodes = malloc( sizeof(char *) * (argc - optind +1));
   NodeSKs = malloc( sizeof(int) * (argc - optind +1));
   for(NodeCount=0; optind < argc; NodeCount++) {
      Nodes[NodeCount] = strdup(argv[optind++]);
   }
   Nodes[NodeCount] = NULL;
   return 0;
}

int main(int argc, char **argv)
{
   if(gethostname(hostname, 64) <0) strcpy(hostname,"NOPNOP");

   /* parse cmdline */
   parse_cmdline(argc, argv);

   /* parse testfile */
   Tests = parse_file(TestFile, test?5:0);
   if( Tests == NULL ) exit(1);
   if(test) exit(0);

   if( NodeCount != Tests->nodecount )
      die("This test needs %d nodes, you only supplied %d\n",
            Tests->nodecount, NodeCount);

   setup_poll();
   /* startup clients */
   startup_clients();
   wait_for_clients();

   /* run test */
   run_test_run();

   return 0;
}
