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
#include "config_gulm.h"
#include "ltpx_priv.h"
#include "ltpx.h"
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

/*****************************************************************************/

/**
 * sigact_usr1 - 
 * @sig: 
 * 
 */
static void sigact_usr1(int sig)
{
   /* dump Lock Queues */
   dump_all_master_tables();
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
 * ltxp_main - 
 * @argc: 
 * @argv: 
 * 
 * 
 * Returns: int
 */
int ltpx_main(int argc, char **argv)
{
   setupsignals();

#ifndef DEBUG
   pid_lock(gulm_config.lock_file, ProgramName);
#endif

   log_msg(lgm_Network2, "ltpx started.\n");

   init_ltpx_poller();
   /* proxy port should be in the config. */
   open_ltpx_listener( gulm_config.ltpx_port );
   if( open_ltpx_to_core() != 0 ) return -1;

   SIGTERM_TRIPPED = FALSE; /*jic*/

   initialize_ltpx_maps();

   /* handling incomming packets */
   ltpx_main_loop();

   clear_pid(gulm_config.lock_file, ProgramName);

   log_msg(lgm_Network, "finished.\n");
   return 0;
}

/* vim: set ai cin et sw=3 ts=3 : */
