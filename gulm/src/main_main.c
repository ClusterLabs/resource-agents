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
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <execinfo.h>
#include <pwd.h>
#include <syslog.h>
#include <libgen.h>

#include "gulm_defines.h"
#include "config_gulm.h"
#include "myio.h"
#include "utils_ip.h"
#include "utils_verb_flags.h"
#include "utils_tostr.h"
#include "lock.h"
#include "core.h"
#include "ltpx.h"

/*****************************************************************************/
/* bits of data used by the log_*() and die() functions. */
uint32_t verbosity=0; /* default is set below in the cmdline args parsing. */
char *ProgramName;

gulm_config_t gulm_config;

/* these are calculated at run time.
 * I will need to do something in the future for nodes with multiple ips.
 * */
char myName[256] = "\0";
struct in6_addr myIP = IN6ADDR_ANY_INIT;

/*****************************************************************************/

/**
 * forker - 
 * @name: 
 * 
 * Returns: int
 */
int forker(char *name, int argc, char **argv)
{
   pid_t pid;
   char *old;

   if((pid=fork()) == 0) {
      /* child */

      old = argv[0]; /* exec this. */
      argv[0] = name; /* but be called this */

      execvp(old, argv);

      printf("%s Should not be here. %s %m\n", ProgramName, name);

   }else if( pid > 0 ) {
      /* parent */
      log_msg(lgm_Forking, "Forked %s.\n", name);
   }else{
      /*error*/
      log_err("Failed to fork %s. %d:%s\n", name, errno, strerror(errno));
   }
   return 0;
}

/**
 * my_daemonize - 
 */
void my_daemonize(void)
{
   pid_t pid;
   int fd, i;

   if( gulm_config.daemon_fork ) {
      if((pid=fork())<0)
         { die(ExitGulm_ExecError,
               "Failed to fork off of parent: %s\n",strerror(errno)); }
      else if(pid!=0) exit(ExitGulm_Ok);
   }

   setsid();
   chdir("/");
   umask(0);

   if( gulm_config.leave_std_open ) {
      for(i=open_max()-1; i>=3; --i) close(i); /* close nearly everything */
      close(0); /* close stdin */
      fd = open("/dev/null", O_RDWR ); /* stdin */
      /* stdout and stderr should still be open to the terminal for debug
       * output.
       */
   }else{
      for(i=open_max()-1; i>=0; --i) close(i); /* close everything */
      fd = open("/dev/null", O_RDWR ); /*stdin*/
      dup(fd); /*stdout*/
      dup(fd); /*stderr*/
   }
   fprintf(stderr, "stderr was left open.\n");
   printf("stdout was left open.\n");

   openlog(ProgramName, LOG_PID, LOG_DAEMON);
}

/**
 * become_nobody - root is bad.
 *
 * we don't need to be root, so don't.
 * well, switch to the user that the config says. try atleast.
 * 
 */
void become_nobody(void)
{
   struct passwd *pw=NULL;
   if( getuid() == 0 ) { /* we are root */
      nice(-10); /* while we're here.... */
      /* is run_as root? */
      if( strcmp(gulm_config.run_as, "root") == 0) return;
      if( (pw = getpwnam(gulm_config.run_as)) != NULL ) {
         setuid(pw->pw_uid);
         setgid(pw->pw_gid);
      }else
      {
         die(ExitGulm_InitFailed,
               "There is no user id \"%s\", cannot continue.\n",
               gulm_config.run_as);
      }
   }
}

/**
 * set_myID - Determin who I am.
 * 
 */
void set_myID(void)
{
   /* cute tricks to set the default name and IP. */
   gethostname(myName, 256);
   /* lookup my ip from my full name. */
   if( get_ip_for_name(myName, &myIP) != 0 )
      die(ExitGulm_InitFailed, "Failed to find IP for my name (%s)\n",
            myName);
}

/**
 * banner_msg - 
 * 
 * Prints out a bunch of things, mostly stuff so users and support can see
 * right away what's going on. Or something like that.
 * 
 * wonder if i should banner each part of gulm?
 */
void banner_msg(void)
{
   log_msg(lgm_Always, "Starting %s %s. (built " __DATE__" " __TIME__ ")\n"
         "Copyright (C) 2004 Red Hat, Inc.  All rights reserved.\n",
         ProgramName, RELEASE);
   log_msg(lgm_Always, "You are running in %s mode.\n",
         gulm_config.fog?"Fail-over":"Standard" );
   log_msg(lgm_Always, "I am (%s) with ip (%s)\n", myName, ip6tostr(&myIP));
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

   ProgramName = strdup(basename(argv[0]));
   if( ProgramName == NULL ) die(ExitGulm_NoMemory, "Out of Memory.\n");
   set_verbosity("Default", &verbosity);

   /* parse cmdline (and config) */
   if( parse_conf(&gulm_config, argc, argv) != 0 ) return -1;

   /*splits*/
   if( strcmp("lock_gulmd", ProgramName) == 0 ) {
      openlog("lock_gulmd_main", LOG_PID, LOG_DAEMON);

      /* fork core */
      forker("lock_gulmd_core", argc, argv);

      /* just a moment...*/
      sleep(1);

      /* fork lt */
      forker("lock_gulmd_LT", argc, argv);

      /* just a moment...*/
      sleep(1);

      /* fork ltproxy */
      forker("lock_gulmd_LTPX", argc, argv);

      return 0;
   }

   /* daemonize ourselves here */
   become_nobody();

   set_myID();

   my_daemonize();
   banner_msg();

   if( strcmp("lock_gulmd_core", ProgramName) == 0 ) {
      return core_main(argc, argv);
   }else
   if( strcmp("lock_gulmd_LTPX", ProgramName) == 0 ) {
      return ltpx_main(argc, argv);
   }else
   if( strcmp("lock_gulmd_LT", ProgramName) == 0 ) {
      return lt_main(argc, argv);
   }

   /* all done. */

   return 0;
}

/* vim: set ai cin et sw=3 ts=3 : */
