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
#include "utils_verb_flags.h"
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

/**
 * parse_ccs - 
 * @gf: 
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
   int cd;
   char *tmp;

   if( (tmp=getenv("GULMD_SKIPCCS"))!=NULL) return 0;

   if( (cd=ccs_force_connect(gf->clusterID, 0)) < 0 ) {
      fprintf(stderr, "No ccsd, checking for cmdline config. (%d:%s)\n",
            cd, strerror(abs(cd)));
      return -1;
   }

   if( ccs_get(cd, "/cluster/@name", &tmp) == 0 ) {
      strdup_with_free((char**)&gf->clusterID, tmp);
      setenv("GULMD_NAME", tmp, 1);
      free(tmp);
   }
   if( ccs_get(cd, "/cluster/gulm/servers", &tmp) == 0 ) {
      parse_cmdline_servers(gf, tmp);
      setenv("GULMD_SERVERS", tmp, 1);
      free(tmp);
   }
   if( ccs_get(cd, "/cluster/gulm/verbosity", &tmp) == 0 ) {
      setenv("GULMD_VERBOSITY", tmp, 1);
      free(tmp);
   }
   if( ccs_get(cd, "/cluster/gulm/heartbeat_rate", &tmp) == 0 ) {
      setenv("GULMD_HEARTBEAT_RATE", tmp, 1);
      free(tmp);
   }
   if( ccs_get(cd, "/cluster/gulm/allowed_misses", &tmp) == 0 ) {
      setenv("GULMD_ALLOWED_MISSES", tmp, 1);
      free(tmp);
   }
   if( ccs_get(cd,"/cluster/gulm/new_connection_timeout", &tmp)==0) {
      setenv("GULMD_NEW_CONNECTION_TIMEOUT", tmp, 1);
      free(tmp);
   }
   if( ccs_get(cd, "/cluster/gulm/master_scan_delay", &tmp) == 0 ) {
      setenv("GULMD_MASTER_SCAN_DELAY", tmp, 1);
      free(tmp);
   }
   if( ccs_get(cd, "/cluster/gulm/coreport", &tmp) == 0 ) {
      setenv("GULMD_COREPORT", tmp, 1);
      free(tmp);
   }
   if( ccs_get(cd, "/cluster/gulm/ltpxport", &tmp) == 0 ) {
      setenv("GULMD_LTPXPORT", tmp, 1);
      free(tmp);
   }
   if( ccs_get(cd, "/cluster/gulm/ltport", &tmp) == 0 ) {
      setenv("GULMD_LTPORT", tmp, 1);
      free(tmp);
   }
   if( ccs_get(cd, "/cluster/gulm/fence_bin", &tmp) == 0 ) {
      setenv("GULMD_FENCE_BIN", tmp, 1);
      free(tmp);
   }
   if( ccs_get(cd, "/cluster/gulm/run_as", &tmp) == 0 ) {
      setenv("GULMD_RUN_AS", tmp, 1);
      free(tmp);
   }
   if( ccs_get(cd, "/cluster/gulm/lock_dir", &tmp) == 0 ) {
      setenv("GULMD_LOCK_DIR", tmp, 1);
      free(tmp);
   }
   if( ccs_get(cd, "/cluster/gulm/lt_partitions", &tmp) == 0 ) {
      setenv("GULMD_LT_PARTITIONS", tmp, 1);
      free(tmp);
   }
   if( ccs_get(cd, "/cluster/gulm/lt_high_locks", &tmp) == 0 ) {
      setenv("GULMD_LT_HIGH_LOCKS", tmp, 1);
      free(tmp);
   }
   if( ccs_get(cd, "/cluster/gulm/lt_drop_req_rate", &tmp) == 0 ) {
      setenv("GULMD_LT_DROP_REQ_RATE", tmp, 1);
      free(tmp);
   }
   if( ccs_get(cd, "/cluster/gulm/prealloc_locks", &tmp) == 0 ) {
      setenv("GULMD_PREALLOC_LOCKS", tmp, 1);
      free(tmp);
   }
   if( ccs_get(cd, "/cluster/gulm/prealloc_holders", &tmp) == 0 ) {
      setenv("GULMD_PREALLOC_HOLDERS", tmp, 1);
      free(tmp);
   }
   if( ccs_get(cd, "/cluster/gulm/prealloc_lkrqs", &tmp) == 0 ) {
      setenv("GULMD_PREALLOC_LKRQS", tmp, 1);
      free(tmp);
   }

   ccs_disconnect(cd);

   setenv("GULMD_SKIPCCS","TRUE",1);

   return -1;
}

/* vim: set ai cin et sw=3 ts=3 : */
