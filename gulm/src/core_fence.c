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
#include <signal.h>
#include <sys/wait.h>
#include <sys/time.h>

#include "gulm_defines.h"
#include "LLi.h"
#include "gio_wiretypes.h"
#include "core_priv.h"
#include "config_gulm.h"


/*****************************************************************************/
/* bits of data used by the log_*() and die() functions. */
extern uint32_t verbosity;
extern char *ProgramName;

extern gulm_config_t gulm_config;
extern int I_am_the;

LLi_t  StomithPids;

typedef struct pid_list_s {
   LLi_t pid_list;
   pid_t pid;
   uint8_t *Name;
} pidlist_t;

/*****************************************************************************/

void init_fence(void)
{
   LLi_init_head(&StomithPids);
}

/**
 * fence_node - call out to fence a missbeahved node
 * @name: < the name of the node
 * @pause: < seconds to wait before killing
 * 
 * This passes a node name to a fence program.  The fence program scans its
 * own conffile file and decides how to do the fence action.
 * 
 * Returns: pid_t
 */
pid_t fence_node(char *name, int pause)
{
   pid_t pid;
   int i;
   char *argv[3];

   argv[0] = gulm_config.fencebin;
   argv[1] = name;
   argv[2] = NULL;

   if((pid=fork()) == 0) {
      /* child */
      if( pause > 0) sleep(pause);

      log_msg(lgm_Forking, "Gonna exec %s %s\n", argv[0], argv[1] );

      for(i=open_max()-1; i>=3; --i) close(i); /* close everything but stds */

      execvp(argv[0], argv);
      fprintf(stderr,"ERROR Failed to execvp. %d:%s\n", errno, strerror(errno));
      _exit(ExitGulm_ExecError);/*jic*/
   }else if(pid>0) {
      /*parent*/
      log_msg(lgm_Forking, "Forked [%d] %s %s with a %d pause.\n",
            pid, argv[0], argv[1], pause);
   }else{
      /*error*/
      log_msg(lgm_Forking, "Problems (%d:%s) trying to start: %s %s\n",
            errno, strerror(errno),
            argv[0], argv[1]);
      pid = -1;
   }
   return pid;
}

/**
 * queue_node_for_fencing - 
 * @Name: 
 * 
 * I only wonder if I should handle the ENOMEM differently....
 * YES.
 * 
 */
void queue_node_for_fencing(uint8_t *Name)
{
   pidlist_t *pdl;

   pdl = malloc(sizeof(pidlist_t));
   if( pdl == NULL ) die(ExitGulm_NoMemory,"Out of memory.\n");
   LLi_init(&pdl->pid_list, pdl);
   /* we *MUST* get the fence bin forked off.  So we try until we get it.
    * This could be bad.  But if you've used up that many resources all
    * ready...
    */
   while( (pdl->pid = fence_node(Name, 0)) < 0 ) sleep(1);
   pdl->Name = strdup(Name);
   if( pdl->Name == NULL ) die(ExitGulm_NoMemory,"Out of memory.\n");

   LLi_add_after(&StomithPids, &pdl->pid_list);
}


/**
 * check_for_zombied_stomiths - 
 * @pid: 
 * @status: 
 * 
 * Returns: =0: NotFound =1: Found
 */
int check_for_zombied_stomiths(pid_t pid, int status)
{
   LLi_t *tmp;
   for(tmp = LLi_next(&StomithPids);
       NULL != LLi_data(tmp);
       tmp = LLi_next(tmp) ){
      pidlist_t *pdl;
      pdl = LLi_data(tmp);
      if( pdl->pid == pid ) {

         if( I_am_the != gio_Mbr_ama_Master &&
             I_am_the != gio_Mbr_ama_Arbitrating) {
            /* don't care how it returned, we're not supposed to be runnign
             * fence actions anymore, so just eat it.
             * How did this happen?  If we were Slave, and Master
             * died, and we got to Arbit, we would call out to fence
             * oldMaster.  But after we make that callout, we find
             * another Arbitrator that ranks us, so we switch to Slave.
             * And now we're Slave with a pending fence action.
             */
            log_msg(lgm_Stomith, "found match on pid %d, ignoring since "
                  "we're not Master or Arbitrator.\n", pid);
            LLi_del(tmp);
            free(pdl->Name);
            free(pdl);
         }else
         if( (WIFEXITED(status) && WEXITSTATUS(status) == 0) ) {
            log_msg(lgm_Stomith,"found match on pid %d, marking node %s as "
                  "logged out.\n", pid, pdl->Name);

            Mark_lgout_from_Exp(pdl->Name);

            /* Bcast NodeX Killed to subscribers.
             */
            send_mbrshp_to_slaves(pdl->Name, gio_Mbr_Killed);
            send_mbrshp_to_children(pdl->Name, gio_Mbr_Killed);

            LLi_del(tmp);
            free(pdl->Name);
            free(pdl);
         }else{
            if( WIFEXITED(status) ) {
               log_msg(lgm_Stomith,"Fence failed. [%d] Exit code:%d "
                     "Running it again.\n", pid, WEXITSTATUS(status));
            }else
            if( WIFSIGNALED(status) ) {
               log_msg(lgm_Stomith,"Fence failed. [%d] Signal:%d "
                     "Running it again.\n", pid, WTERMSIG(status));
            }else
            if( WIFSTOPPED(status) ) {
               log_msg(lgm_Stomith,"Fence stopped. [%d] Signal:%d "
                     "Running it again.\n", pid, WSTOPSIG(status));
            }else
               log_msg(lgm_Stomith, "Fence failed [%d] for unknown reason. "
                     "Running it again.\n", pid);

            pdl->pid = fence_node(pdl->Name, 5);

         }
         return 1; /* found something. stop. */
      }
   }
   return 0;
}
/* vim: set ai cin et sw=3 ts=3 : */
