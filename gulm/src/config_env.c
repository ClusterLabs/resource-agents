/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
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
#include <sys/stat.h>
#include <getopt.h>

#include "gulm_defines.h"
#include "config_gulm.h"
#include "config_priv.h"
#include "LLi.h"
#include "utils_ip.h"
#include "utils_dir.h"
#include "utils_crc.h"
#include "utils_verb_flags.h"

/*****************************************************************************/
/* bits of data used by the log_*() and die() functions. */
extern uint32_t verbosity;
extern char *ProgramName;

/*****************************************************************************/


void parse_env(gulm_config_t *gf)
{
   char *match=NULL;
   uint64_t temp;

   if((match=getenv("GULMD_NAME"))!=NULL) {
      strdup_with_free((char**)&gf->clusterID, match);
   }
   if((match=getenv("GULMD_SERVERS"))!=NULL) {
      parse_cmdline_servers(gf, match);
   }
   if((match=getenv("GULMD_VERBOSITY"))!=NULL) {
      set_verbosity(match, &verbosity);
   }
   if((match=getenv("GULMD_HEARTBEAT_RATE"))!=NULL) {
      temp = ft2uint64(atof(match));
      gf->heartbeat_rate = bound_to_uint64(temp, 75000, (uint64_t)~0);
   }
   if((match=getenv("GULMD_ALLOWED_MISSES"))!=NULL) {
      gf->allowed_misses = bound_to_uint16(atoi(match), 1, 0xffff);
   }
   if((match=getenv("GULMD_NEW_CONNECTION_TIMEOUT"))!=NULL) {
      temp = ft2uint64(atof(match));
      gf->new_con_timeout = bound_to_uint64(temp, 0, (uint64_t)~0);
   }
   if((match=getenv("GULMD_MASTER_SCAN_DELAY"))!=NULL) {
      temp = ft2uint64(atof(match));
      gf->master_scan_delay = bound_to_uint64(temp, 10, (uint64_t)~0);
   }
   if((match=getenv("GULMD_COREPORT"))!=NULL) {
      gf->corePort = atoi(match);
   }
   if((match=getenv("GULMD_LTPXPORT"))!=NULL) {
      gf->ltpx_port = atoi(match);
   }
   if((match=getenv("GULMD_LTPORT"))!=NULL) {
      gf->lt_port = atoi(match);
   }
   if((match=getenv("GULMD_FENCE_BIN"))!=NULL) {
      strdup_with_free((char**)&gf->fencebin, match);
   }
   if((match=getenv("GULMD_RUN_AS"))!=NULL) {
      strdup_with_free((char**)&gf->run_as, match);
   }
   if((match=getenv("GULMD_LOCK_DIR"))!=NULL) {
      strdup_with_free((char**)&gf->lock_file, match);
   }
   if((match=getenv("GULMD_LT_PARTITIONS"))!=NULL) {
      gf->how_many_lts = bound_to_uint16(atoi(match), 1, 256);
   }
   if((match=getenv("GULMD_LT_HIGH_LOCKS"))!=NULL) {
      gf->lt_maxlocks = bound_to_ulong(atoi(match), 10000, ~0UL);
   }
   if((match=getenv("GULMD_LT_DROP_REQ_RATE"))!=NULL) {
      gf->lt_cf_rate = bound_to_uint(atoi(match), 5, ~0U);
   }
   if((match=getenv("GULMD_PREALLOC_LOCKS"))!=NULL) {
      gf->lt_prelocks = bound_to_uint(atoi(match), 0, ~0U);
   }
   if((match=getenv("GULMD_PREALLOC_HOLDERS"))!=NULL) {
      gf->lt_preholds = bound_to_uint(atoi(match), 0, ~0U);
   }
   if((match=getenv("GULMD_PREALLOC_LKRQS"))!=NULL) {
      gf->lt_prelkrqs = bound_to_uint(atoi(match), 0, ~0U);
   }
}


/* vim: set ai cin et sw=3 ts=3 : */
