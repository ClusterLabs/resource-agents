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

#include "gulm_defines.h"
#include "config_gulm.h"
#include "config_priv.h"
#include "ccs.h"

/* Mostly, this is the extra functions I need to get stuff from the ccslib.
 * And a few wrapper functions so things work cleanly with it.
 */

/*****************************************************************************/
/* First data that is stored in the main. */

/* bits of data used by the log_*() and die() functions. */
extern uint32_t verbosity;
extern char *ProgramName;

/* confed things. */
extern gulm_config_t gulm_config;
extern char myName[256];

/*****************************************************************************/

int extendargv(int *argc, char ***argv, int growth)
{
   char **temp;
   if( growth <= *argc ) return 0;

   temp = realloc(*argv, growth * sizeof(char**));
   if( temp == NULL ) return -ENOMEM;

   *argv = temp;
   *argc = growth;

   return 0;
}

void push_opts(char *name, char *value, int *argc, char ***argv, int *next)
{
   if( *next >= *argc ) {
      if( extendargv(argc, argv, *next + 10 ) != 0 )
         die(1, "Out of Memory.\n");
   }

   (*argv)[(*next)++] = name;
   (*argv)[(*next)++] = value;
}

/**
 * parse_ccs - 
 * @gf: 
 * 
 * Thinking to have this actually just build an array of the strings it
 * pulls out of the ccs, and format that array as cmdline args, then pass
 * that to parse_cmdline().  ccs gives me strings, and I already wrote all
 * that sctring parsing stuff over there, so why not just reused it?
 * (possibly weird error messages.)
 *
 * Of course, one must wonder if we should do things in a way that is a bit
 * more xml friendly.  Mostly instead of:
 *  <servers>a,b,c</servers>
 * have:
 *  <server>a</server> <server>b</server> <server>c</server>
 * 
 * Returns: int
 */
int parse_ccs(gulm_config_t *gf)
{
   int ccs_argc=0, next=0;
   char **ccs_argv=NULL, *tmp=NULL;

   if( (gf->ccs_desc=ccs_force_connect(NULL, 1/*?blocking?*/)) < 0 ) {
      fprintf(stderr, "No ccs, checking for cmdline config. (%d:%s)\n",
            gf->ccs_desc, strerror(abs(gf->ccs_desc)));
      gf->ccs_desc = -1;
      return -1;
   }

   if( ccs_get(gf->ccs_desc, "/cluster/@name", &tmp) == 0 )
      push_opts("--cluster_name", tmp, &ccs_argc, &ccs_argv, &next);

   if( ccs_get(gf->ccs_desc, "/cluster/gulm/servers", &tmp) == 0 )
      push_opts("--servers", tmp, &ccs_argc, &ccs_argv, &next);

   if( ccs_get(gf->ccs_desc, "/cluster/gulm/verbosity", &tmp) == 0 )
      push_opts("--verbosity", tmp, &ccs_argc, &ccs_argv, &next);

   if( ccs_get(gf->ccs_desc, "/cluster/gulm/heartbeat_rate", &tmp) == 0 )
      push_opts("--heartbeat_rate", tmp, &ccs_argc, &ccs_argv, &next);

   if( ccs_get(gf->ccs_desc, "/cluster/gulm/allowed_misses", &tmp) == 0 )
      push_opts("--allowed_misses", tmp, &ccs_argc, &ccs_argv, &next);

   if( ccs_get(gf->ccs_desc, "/cluster/gulm/new_connection_timeout", &tmp) == 0 )
      push_opts("--new_connection_timeout", tmp, &ccs_argc, &ccs_argv, &next);

   if( ccs_get(gf->ccs_desc, "/cluster/gulm/master_scan_delay", &tmp) == 0 )
      push_opts("--master_scan_delay", tmp, &ccs_argc, &ccs_argv, &next);

   if( ccs_get(gf->ccs_desc, "/cluster/gulm/coreport", &tmp) == 0 )
      push_opts("--coreport", tmp, &ccs_argc, &ccs_argv, &next);

   if( ccs_get(gf->ccs_desc, "/cluster/gulm/ltpxport", &tmp) == 0 )
      push_opts("--ltpxport", tmp, &ccs_argc, &ccs_argv, &next);

   if( ccs_get(gf->ccs_desc, "/cluster/gulm/ltport", &tmp) == 0 )
      push_opts("--ltport", tmp, &ccs_argc, &ccs_argv, &next);

   if( ccs_get(gf->ccs_desc, "/cluster/gulm/fence_bin", &tmp) == 0 )
      push_opts("--fence_bin", tmp, &ccs_argc, &ccs_argv, &next);

   if( ccs_get(gf->ccs_desc, "/cluster/gulm/run_as", &tmp) == 0 )
      push_opts("--run_as", tmp, &ccs_argc, &ccs_argv, &next);

   if( ccs_get(gf->ccs_desc, "/cluster/gulm/lock_dir", &tmp) == 0 )
      push_opts("--lock_dir", tmp, &ccs_argc, &ccs_argv, &next);

   if( ccs_get(gf->ccs_desc, "/cluster/gulm/lt_partitions", &tmp) == 0 )
      push_opts("--lt_partitions", tmp, &ccs_argc, &ccs_argv, &next);

   if( ccs_get(gf->ccs_desc, "/cluster/gulm/lt_high_locks", &tmp) == 0 )
      push_opts("--lt_high_locks", tmp, &ccs_argc, &ccs_argv, &next);

   if( ccs_get(gf->ccs_desc, "/cluster/gulm/lt_drop_req_rate", &tmp) == 0 )
      push_opts("--lt_drop_req_rate", tmp, &ccs_argc, &ccs_argv, &next);

   if( ccs_get(gf->ccs_desc, "/cluster/gulm/prealloc_locks", &tmp) == 0 )
      push_opts("--prealloc_locks", tmp, &ccs_argc, &ccs_argv, &next);

   if( ccs_get(gf->ccs_desc, "/cluster/gulm/prealloc_holders", &tmp) == 0 )
      push_opts("--prealloc_holders", tmp, &ccs_argc, &ccs_argv, &next);

   if( ccs_get(gf->ccs_desc, "/cluster/gulm/prealloc_lkrqs", &tmp) == 0 )
      push_opts("--prealloc_lkrqs", tmp, &ccs_argc, &ccs_argv, &next);

   push_opts(NULL, NULL, &ccs_argc, &ccs_argv, &next);

   parse_cmdline(gf, next, ccs_argv);

   for(; next > 0 ; next--) {
      /* items with -- are not malloced. */
      if( ccs_argv[next] != NULL &&
          ccs_argv[next][0] != '-' &&
          ccs_argv[next][1] != '-' )
         free(ccs_argv[next]);
   }
   free(ccs_argv);

   return 0;
}

/**
 * verify_name_and_ip_ccs - 
 * @name: 
 * @ip: 
 * 
 * Returns: =0:Deny =1:Allow
 */
int verify_name_and_ip_ccs(char *name, struct in6_addr *ip)
{
   int n, ret=1;
   char req[256], *tmp=NULL;

   if( gulm_config.ccs_desc < 0 ) return 1;

   n = snprintf(req, 256, "/nodes/node[@name='%s']", name);
   if( n < 0 || n > 255 ) {
      log_msg(lgm_Network2,"snprintf failed\n");
      ret = 0;
      goto fail; /* snprintf failed */
   }

   if( ccs_get(gulm_config.ccs_desc, req, &tmp) != 0 ) {
      ret = 0;
   }
   if(tmp!=NULL)free(tmp);

fail:
   return ret;
}

/* vim: set ai cin et sw=3 ts=3 : */
