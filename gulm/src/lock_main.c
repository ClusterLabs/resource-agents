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
#include <signal.h>
#include <execinfo.h>

#include "gulm_defines.h"
#include "lock_priv.h"
#include "config_gulm.h"
#include "utils_dir.h"

/*****************************************************************************/
/* bits of data used by the log_*() and die() functions. */
extern uint32_t verbosity;
extern char *ProgramName;
/* other bits of interest */
extern gulm_config_t gulm_config;
extern int SIGTERM_TRIPPED;
extern int I_am_the;
extern struct in6_addr myIP;
/* */
char *LTname = NULL;
int LTid = 0;

/*****************************************************************************/

/**
 * sigact_usr1 - 
 * @sig: 
 * 
 */
static void sigact_usr1(int sig)
{
   /* dump LockTables */
   dump_locks();
}

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
#ifndef DEBUG
      syslog(LOG_NOTICE, "BACKTRACE\n");
#else
      fprintf(stderr, "BACKTRACE\n");
#endif
   for(i=0;i<size; i++)
#ifndef DEBUG
      syslog(LOG_NOTICE, " %s\n", strings[i]);
#else
      fprintf(stderr, " %s\n", strings[i]);
#endif
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
   act.sa_handler = SIG_IGN; /* use gulm_tool shutdown or kill -9 */
   if( sigaction(SIGTERM, &act, NULL) <0)
      die(ExitGulm_InitFailed,
            "Failed to install signal SIGTERM handler: %s\n",strerror(errno));

   memset(&act,0,sizeof(act));
   act.sa_handler = sigact_usr1;
   if( sigaction(SIGUSR1, &act, NULL) <0)
      die(ExitGulm_InitFailed,
            "Failed to install signal SIGUSR1 handler: %s\n",strerror(errno));

   memset(&act,0,sizeof(act));
   act.sa_handler = SIG_IGN;
   if( sigaction(SIGUSR2, &act, NULL) <0)
      die(ExitGulm_InitFailed,
            "Failed to install signal SIGUSR2 handler: %s\n",strerror(errno));

   memset(&act,0,sizeof(act));
   act.sa_handler = SIG_IGN;
   if( sigaction(SIGHUP, &act, NULL) <0)
      die(ExitGulm_InitFailed,
            "Failed to install signal SIGHUP handler: %s\n",strerror(errno));

   memset(&act,0,sizeof(act));
   act.sa_handler = SIG_IGN;/* don't die on broken pipes.*/
   if( sigaction(SIGPIPE, &act, NULL) <0)
      die(ExitGulm_InitFailed,
            "Failed to install signal SIGPIPE handler: %s\n",strerror(errno));

   memset(&act,0,sizeof(act));
   act.sa_handler = SIG_IGN;
   if( sigaction(SIGALRM, &act, NULL) <0)
      die(ExitGulm_InitFailed,
            "Failed to install signal SIGALRM handler: %s\n",strerror(errno));

   memset(&act,0,sizeof(act));
   act.sa_handler = sigact_segv;
   if( sigaction(SIGSEGV, &act, NULL) <0)
      die(ExitGulm_InitFailed,
            "Failed to install signal SIGSEGV handler: %s\n",strerror(errno));

}

/**
 * twistName - Append '_LTxxx' to our name for logging.
 */
static void twistName(void)
{
   int l;
   char *c;

   LTname = malloc(6);
   if( LTname == NULL ) die(ExitGulm_NoMemory, "Out of Memory.\n");
   snprintf(LTname, 6, "%03d", LTid);

   l = strlen(ProgramName) + 8;
   c = realloc(ProgramName, l);
   if( c != NULL ) {
      ProgramName = c;
      strcat(ProgramName, LTname); /* LT000 */
   }
#ifndef DEBUG
   openlog(ProgramName, LOG_PID, LOG_DAEMON);
#endif /*DEBUG*/
}

/**
 * lt_main - 
 * @argc: 
 * @argv: 
 * 
 * 
 * Returns: int
 */
int lt_main(int argc, char **argv)
{
   int i;
   pid_t pid;

   /* running a server here? */
   if( ! Can_I_be_a_master(&gulm_config, &myIP) ) {
      twistName();
      log_msg(lgm_Always, "Not serving locks from this node.\n");
      return 0;
   }

   if( gulm_config.how_many_lts >= 2 ) {
      for(i = 1; i < gulm_config.how_many_lts ; i++) {
         if((pid=fork()) == 0 ) {
            /* child */
            LTid = i; /* which are we? */
            break;
         }else if(pid > 0 ) {
            /* parent */
         }else {
            /* error */
         }
      }
   }

   /* twist the logging a wee bit.  Mostly just get the letters LT into
    * the name.
    */
   twistName();

#ifndef DEBUG
   pid_lock(gulm_config.lock_file, ProgramName);
#endif

   log_msg(lgm_Network2, "Locktable %d started.\n", LTid);

   /* we're the child of a daemon, so we've been daemonized.
    * set up signal handlers.
    */
   setupsignals();

   init_lt_poller();
   open_lt_listener( gulm_config.lt_port + LTid );
   if( open_lt_to_core() != 0 ) return -1;
   init_lockspace(gulm_config.lt_maxlocks, gulm_config.lt_hashbuckets);
   SIGTERM_TRIPPED = FALSE; /*jic*/

   /* handling incomming packets */
   lt_main_loop();

   clear_pid(gulm_config.lock_file, ProgramName);

   log_msg(lgm_Network, "finished.\n");
   return 0;
}

/* vim: set ai cin et sw=3 ts=3 : */
