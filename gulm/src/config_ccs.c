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
   uint64_t temp;
   char *tmp;

   if( gf->clusterID == NULL ) {
      fprintf(stderr, "Warning! You didn't specify a cluster name before "
            "--use_ccs\n  Letting ccsd choose which cluster we belong to.\n");
   }

   if( (cd=ccs_force_connect(gf->clusterID, 0)) < 0 ) {
      fprintf(stderr, "No ccsd, checking for cmdline config. (%d:%s)\n",
            cd, strerror(abs(cd)));
      cd = -1;
      return -1;
   }

   if( ccs_get(cd, "/cluster/@name", &tmp) == 0 ) {
      strdup_with_free((char**)&gf->clusterID, tmp);
      free(tmp);
   }

   if( ccs_get(cd, "/cluster/gulm/servers", &tmp) == 0 ) {
      parse_cmdline_servers(gf, tmp);
      free(tmp);
   }

   if( ccs_get(cd, "/cluster/gulm/verbosity", &tmp) == 0 ) {
      set_verbosity(tmp, &verbosity);
      free(tmp);
   }

   if( ccs_get(cd, "/cluster/gulm/heartbeat_rate", &tmp) == 0 ) {
      temp = ft2uint64(atof(tmp));
      gf->heartbeat_rate = bound_to_uint64(temp, 75000, (uint64_t)~0);
      /* min is 0.075 */
      free(tmp);
   }

   if( ccs_get(cd, "/cluster/gulm/allowed_misses", &tmp) == 0 ) {
      gf->allowed_misses = bound_to_uint16(atoi(tmp), 1, 0xffff);
      free(tmp);
   }

   if( ccs_get(cd,"/cluster/gulm/new_connection_timeout", &tmp)==0) {
      temp = ft2uint64(atof(tmp));
      gf->new_con_timeout = bound_to_uint64(temp, 0, (uint64_t)~0);
      /* min should be something bigger than zero...
       * say 0.5? why?
       */
      free(tmp);
   }

   if( ccs_get(cd, "/cluster/gulm/master_scan_delay", &tmp) == 0 ) {
      temp = ft2uint64(atof(tmp));
      gf->master_scan_delay = bound_to_uint64(temp, 10, (uint64_t)~0);
      free(tmp);
   }

   if( ccs_get(cd, "/cluster/gulm/coreport", &tmp) == 0 ) {
      gf->corePort = atoi(tmp);
      free(tmp);
   }

   if( ccs_get(cd, "/cluster/gulm/ltpxport", &tmp) == 0 ) {
      gf->ltpx_port = atoi(tmp);
      free(tmp);
   }

   if( ccs_get(cd, "/cluster/gulm/ltport", &tmp) == 0 ) {
      gf->lt_port = atoi(tmp);
      free(tmp);
   }

   if( ccs_get(cd, "/cluster/gulm/fence_bin", &tmp) == 0 ) {
      strdup_with_free((char**)&gf->fencebin, tmp);
      free(tmp);
   }

   if( ccs_get(cd, "/cluster/gulm/run_as", &tmp) == 0 ) {
      strdup_with_free((char**)&gf->run_as, tmp);
      free(tmp);
   }

   if( ccs_get(cd, "/cluster/gulm/lock_dir", &tmp) == 0 ) {
      strdup_with_free((char**)&gf->lock_file, tmp);
      free(tmp);
   }

   if( ccs_get(cd, "/cluster/gulm/lt_partitions", &tmp) == 0 ) {
      gf->how_many_lts = bound_to_uint16(atoi(tmp), 1, 256);
      free(tmp);
   }

   if( ccs_get(cd, "/cluster/gulm/lt_high_locks", &tmp) == 0 ) {
      gf->lt_maxlocks = bound_to_ulong(atoi(tmp), 10000, ~0UL);
      free(tmp);
   }

   if( ccs_get(cd, "/cluster/gulm/lt_drop_req_rate", &tmp) == 0 ) {
      gf->lt_cf_rate = bound_to_uint(atoi(tmp), 5, ~0U);
      free(tmp);
   }

   if( ccs_get(cd, "/cluster/gulm/prealloc_locks", &tmp) == 0 ) {
      gf->lt_prelocks = bound_to_uint(atoi(tmp), 0, ~0U);
      free(tmp);
   }

   if( ccs_get(cd, "/cluster/gulm/prealloc_holders", &tmp) == 0 ) {
      gf->lt_preholds = bound_to_uint(atoi(tmp), 0, ~0U);
      free(tmp);
   }

   if( ccs_get(cd, "/cluster/gulm/prealloc_lkrqs", &tmp) == 0 ) {
      gf->lt_prelkrqs = bound_to_uint(atoi(tmp), 0, ~0U);
      free(tmp);
   }

   ccs_disconnect(cd);
   cd = -1;
   return 0;
}

/* vim: set ai cin et sw=3 ts=3 : */
