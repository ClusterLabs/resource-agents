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
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <execinfo.h>

#include "gulm_defines.h"
#include "myio.h"
#include "core_priv.h"
#include "config_gulm.h"
#include "utils_ip.h"
#include "utils_verb_flags.h"
#include "utils_tostr.h"
#include "utils_dir.h"

/*****************************************************************************/
/* bits of data used by the log_*() and die() functions. */
extern uint32_t verbosity;
extern char *ProgramName;

/* signal checks. */
int SIGTERM_TRIPPED = FALSE;
int SIGUSR1_TRIPPED = FALSE;
int SIGCHLD_TRIPPED = FALSE;

extern gulm_config_t gulm_config;

/* these are calculated at run time.
 * I will need to do something in the future for nodes with multiple ips.
 * */
extern char myName[256];
extern struct in6_addr myIP;

/*****************************************************************************/

/**
 * sigact_usr1 - Dump out internal tables.
 * @sig: 
 */
static void sigact_usr1(int sig)
{
   dump_nodes();
   dump_conf(&gulm_config, FALSE);
   dump_resources();
}

/**
 * sigact_chld - clear up exited children
 * @sig: 
 * 
 */
static void sigact_chld(int sig)
{
   SIGCHLD_TRIPPED = TRUE;
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
   act.sa_handler = SIG_IGN;
   if( sigaction(SIGALRM, &act, NULL) <0)
      die(ExitGulm_InitFailed,
            "Failed to install signal SIGALRM handler: %s\n",strerror(errno));

   memset(&act,0,sizeof(act));
   act.sa_handler = SIG_IGN;/* don't die on broken pipes.*/
   if( sigaction(SIGPIPE, &act, NULL) <0)
      die(ExitGulm_InitFailed,
            "Failed to install signal SIGPIPE handler: %s\n",strerror(errno));

   memset(&act,0,sizeof(act));
   act.sa_handler = sigact_chld;
   act.sa_flags = SA_NOCLDSTOP;
   if( sigaction(SIGCHLD, &act, NULL) <0)
      die(ExitGulm_InitFailed,
            "Failed to install signal SIGCHLD handler: %s\n",strerror(errno));

   memset(&act,0,sizeof(act));
   act.sa_handler = sigact_segv;
   if( sigaction(SIGSEGV, &act, NULL) <0)
      die(ExitGulm_InitFailed,
            "Failed to install signal SIGSEGV handler: %s\n",strerror(errno));

}

/**
 * core_main - 
 * @argc: 
 * @argv: 
 * 
 * Returns: int
 */
int core_main(int argc, char **argv)
{
   /* set up other things. */
   setupsignals();

#ifndef DEBUG
   pid_lock(gulm_config.lock_file, ProgramName);
#endif

   /* initialize memory structures. */
   if( init_nodes() != 0 )
      die(ExitGulm_InitFailed, "init nodelist failed.\n");
   init_fence();
   if( init_resources() != 0 )
      die(ExitGulm_InitFailed, "init services failed.\n");
   if( init_core_poller() != 0 )
      die(ExitGulm_InitFailed, "init poller failed.\n");
   if( open_core_listener(gulm_config.corePort) != 0 )
      die(ExitGulm_InitFailed, "open listener failed. %d:%s\n",
            errno, strerror(errno));

   /* ok, Now get to work. */
   work_loop();

   /* send logout to master node. */
   do_logout();

   release_core_poller();

   clear_pid(gulm_config.lock_file, ProgramName);

   log_msg(lgm_Network, "finished.\n");

   return 0;
}

/* vim: set ai cin et sw=3 ts=3 : */
