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
#include "utils_ip.h"
#include "utils_verb_flags.h"
#include "utils_dir.h"
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
 * scan_cluster_section - 
 * @gf: 
 * @rt: 
 * 
 * 
 * Returns: int
 */
int scan_cluster_section(gulm_config_t *gf, ccs_node_t *rt)
{
   if(gf->clusterID != NULL ) free(gf->clusterID);
   gf->clusterID = strdup( find_ccs_str(rt, "cluster/name", '/',
                                        "cluster") );
   if( gf->clusterID == NULL ) die(ExitGulm_NoMemory, "Out of Memory.\n");

   if( strlen(gf->clusterID) <= 0 )
      die(ExitGulm_ParseFail, "Cluster name \"%s\" is too short\n",
            gf->clusterID);
   if( strlen(gf->clusterID) > 16 )
      die(ExitGulm_ParseFail, "Cluster name \"%s\" is too long\n",
            gf->clusterID);

   return 0;
}

/**
 * scan_server_list - 
 * @gf: 
 * @nd: 
 * 
 * 
 * Returns: int
 */
int scan_server_list(gulm_config_t *gf, ccs_node_t *nd)
{
   ccs_value_t *val;
   ip_name_t *in;

   if( nd == NULL || nd->v == NULL ) {
      fprintf(stderr,
            "I couldn't find a \"cluster { lock_gulm { servers = [] } }\""
            " section.\n");
      return -1;
   }

   for(val = nd->v; val; val = val->next) {
      if( val->type != CCS_STRING ) { return -1; }
      in = malloc(sizeof(ip_name_t));
      if( in == NULL ) return -1;
      LLi_init( &in->in_list, in);

      if( gulm_aton(val->v.str, &in->ip) == 0 ) {
         char nme[64], *c;
         /* they gave a dotted ip, try to find name */
         /* get things into the expected byte order. */
         in->ip = osi_cpu_to_be32(in->ip);
         if( get_name_for_ip(nme, 64, in->ip) != 0 ) {
            fprintf(stderr, "I cannot find the name for ip \"%s\".\n",
                  val->v.str);
            return -1;
         }else {
            nme[63] = '\0'; /* jic */
            c = strstr(nme, ".");
            if( c != NULL ) *c = '\0';
            in->name = strdup(nme);
            if( in->name == NULL ) die(ExitGulm_NoMemory, "Out of memory.\n");
         }
      }else
      if( get_ip_for_name(val->v.str, &in->ip) == 0 ) {
         /* they gave a name, and we just got the ip */
         in->name = strdup(val->v.str);
         if( in->name == NULL ) die(ExitGulm_NoMemory, "Out of memory.\n");
      }else
      {
         /* not an ip or name of an ip */
         free(in);
         fprintf(stderr, "I cannot find the ip of \"%s\".\n",
               val->v.str);
         return -1;
      }
      if( gf->node_cnt < 5 ) {
         LLi_add_before( &gf->node_list, &in->in_list );
         gf->node_cnt ++;
      }else{
         fprintf(stderr, "Skipping server entry \"%s\" since the max of five"
               " has been reached.\n", val->v.str);
         free(in);
      }

   }

   return 0;
}

/**
 * scan_gulm_section - 
 * @gf: 
 * @rt: 
 * 
 * 
 * Returns: int
 */
int scan_gulm_section(gulm_config_t *gf, ccs_node_t *rt)
{
   char *tmpstr;
   float tmp_ft;
#define CGPRE "cluster/lock_gulm/"

   /* find int values */
   gf->corePort = find_ccs_int(rt, CGPRE"coreport", '/', 40040);
   gf->ltpx_port = find_ccs_int(rt, CGPRE"ltpx_port", '/', 40042);
   gf->lt_port = find_ccs_int(rt, CGPRE"lt_base_port", '/', 41040);

   tmp_ft = find_ccs_float(rt, CGPRE"heartbeat_rate", '/', 15.0);
   gf->heartbeat_rate = bound_to_uint64(ft2uint64(tmp_ft), 75000,(uint64_t)~0);

   gf->allowed_misses = bound_to_uint16( find_ccs_int(rt,
            CGPRE"allowed_misses", '/', 2), 1, 0xffff);

   tmp_ft = find_ccs_float(rt, CGPRE"new_connection_timeout", '/', 15.0);
   gf->new_con_timeout = bound_to_uint64(ft2uint64(tmp_ft), 0,(uint64_t)~0);

   tmp_ft = find_ccs_float(rt, CGPRE"master_scan_delay", '/', 1.0);
   gf->master_scan_delay = bound_to_uint64(ft2uint64(tmp_ft), 10, (uint64_t)~0);

   gf->how_many_lts = bound_to_uint16( find_ccs_int(rt,
            CGPRE"lt_partitions", '/', 1), 1, 256);

   gf->lt_hashbuckets = bound_to_uint( find_ccs_int(rt,
            CGPRE"lt_hash_buckets", '/', 65536), 1024, 0xffff);

   gf->lt_maxlocks = bound_to_ulong( find_ccs_int(rt,
            CGPRE"lt_high_locks", '/', 1024 * 1024), 10000, ~0UL);

   gf->lt_prelocks = bound_to_uint( find_ccs_int(rt,
            CGPRE"prealloc_locks", '/', 10 ), 0, ~0U);
   gf->lt_preholds = bound_to_uint( find_ccs_int(rt,
            CGPRE"prealloc_holders", '/', 10 ), 0, ~0U);
   gf->lt_prelkrqs = bound_to_uint( find_ccs_int(rt,
            CGPRE"prealloc_lkrqs", '/', 10 ), 0, ~0U);

   gf->lt_cf_rate = bound_to_uint( find_ccs_int(rt,
            CGPRE"lt_drop_req_rate", '/', 10), 5, ~0U);

   /* find strings that are copied in. */
   if(gf->fencebin != NULL ) free(gf->fencebin);
   gf->fencebin = strdup( find_ccs_str(rt,
            CGPRE"fencebin", '/', "fence_node") );
   if( gf->fencebin == NULL ) die(ExitGulm_NoMemory, "Out of Memory.\n");

   if(gf->run_as != NULL ) free(gf->run_as);
   gf->run_as = strdup( find_ccs_str(rt, CGPRE"run_as", '/', "root") );
   if( gf->run_as == NULL ) die(ExitGulm_NoMemory, "Out of Memory.\n");

   if(gf->lock_file != NULL ) free(gf->lock_file);
   gf->lock_file = strdup( find_ccs_str(rt, CGPRE"lock_dir", '/',
                                        "/var/run/sistina") );
   if( gf->lock_file == NULL ) die(ExitGulm_NoMemory, "Out of Memory.\n");

   /* find strings that are parsed more */
   tmpstr = (char*)find_ccs_str(rt, CGPRE"verbosity", '/', NULL);
   if( tmpstr != NULL ) set_verbosity(tmpstr, &verbosity);

   /* now get the list of servers. */
   if( scan_server_list(gf, find_ccs_node(rt, CGPRE"servers", '/')) < 0 ) {
      release_node_list(&gf->node_list);
      gf->node_cnt = 0;
      LLi_init_head(&gf->node_list);
      return -1;
   }

#undef CGPRE

   return 0;
}

/**
 * parse_ccs - 
 * @gf: 
 * 
 * 
 * Returns: int
 */
int parse_ccs(gulm_config_t *gf)
{
   ccs_node_t *rt;
   int err=0;

   if((err=open_ccs_file(&rt, "cluster.ccs")) != 0 ) {
      fprintf(stderr, "Failed to get \"cluster.ccs\" from ccsd: %d:%d:%s\n",
            err, errno, strerror(errno));
      fprintf(stderr, "Skipping all of ccs for configs.\n");
      return err;
   }

   /* get cluster section info */
   if( (err = scan_cluster_section(gf, rt)) < 0 ) {
      goto exit;
   }

   /* get cluster/lock_gulm info */
   if( (err = scan_gulm_section(gf, rt)) < 0 ) {
      goto exit;
   }

exit:
   close_ccs_file(rt);
   return err;
}

/**
 * verify_name_and_ip_ccs - 
 * @name: 
 * @ip: 
 * 
 * check ccs for node {name {}} entry.
 * (ip has already been validated by libresolv)
 *
 * If there isn't a nodes.ccs file, then say there ok, since that is
 * different from there being a file and the name isn't in it.
 * 
 * Returns: =0:Deny =1:Allow
 */
int verify_name_and_ip_ccs(char *name, uint32_t ip)
{
   ccs_node_t *rt=NULL, *nd=NULL;
   char req[256];
   int n, ret=1;

   if((n=open_ccs_file(&rt, "nodes.ccs")) != 0) {
      log_msg(lgm_Network2,"Failed to open nodes.ccs: %d:%d:%s\n",
            n, errno, strerror(errno));
      ret = 1; /* ccs isn't there, ignore it */
      goto fail;
   }

   n = snprintf(req, 256, "nodes/%s", name);
   if( n < 0 || n > 255 ) {
      log_msg(lgm_Network2,"snprintf failed\n");
      ret = 0;
      goto fail; /* snprintf failed */
   }

   nd = find_ccs_node(rt, req, '/');
   if( nd == NULL ) {
      log_msg(lgm_Network2,"Node name %s is not in CCS. (correct case?)\n",
            name);
      ret = 0;
      goto fail;
   }

fail:
   if( rt != NULL ) close_ccs_file(rt);
   return ret;
}

/* vim: set ai cin et sw=3 ts=3 : */
