/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
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

void parse_cmdline_servers(gulm_config_t *gf, char *servers)
{
   char *workspace = NULL;
   char *token, *next;
   int wl, tl;
   ip_name_t *in;

   if( servers == NULL ) goto exit;

   workspace = strdup(servers);
   if( workspace == NULL ) goto exit;

   /* this `overwrites' previous entries so... */
   if( !LLi_empty(&gf->node_list) ) {
      release_node_list(&gf->node_list);
      gf->node_cnt = 0;
      LLi_init_head(&gf->node_list);
   }

   wl = strlen(workspace);
   if( wl == 0 ) goto exit;/* right, so I'm not using strtok why? */
   for(token = workspace, tl=0; tl < wl &&
         token[tl] != ',' &&
         token[tl] != ' ' &&
         token[tl] != '\0';
         tl++);
   token[tl] = '\0'; /* damn need to names to be terminated.*/
   next = token + tl + 1;

   for(;;) {
      /* We can get empty tokens here given inlined spaces.
       * so tl == 0, is skippable.
       */
      if( tl > 0 ) {
         /* figure out name and ip from whatever they gave us */
         in = get_ipname(token);

         if( in == NULL ) goto exit;
         if( in->name == NULL ) {
            fprintf(stderr, "I cannot find the name for ip \"%s\". Stopping.\n",
                  token);
            goto exit;
         }

         if( gf->node_cnt < 5 ) {
            LLi_add_before( &gf->node_list, &in->in_list );
            gf->node_cnt ++;
         }else{
            fprintf(stderr, "Skipping server entry \"%s\" since the max of five"
                  " has been reached.\n", token);
            free(in);
         }

      }

      if( next >= workspace + wl ) goto exit;
      for(token = next, tl = 0;
            tl < wl &&
            token[tl] != ',' &&
            token[tl] != ' ' &&
            token[tl] != '\0';
            tl++);
      token[tl] = '\0';
      next = token + tl +1;
   }

exit:
   if( workspace != NULL ) free(workspace);
}

void usage(void)
{
   char *strings[] = {
   "Usage:\n",
   "\n",
   "lock_gulmd [options]\n",
   "Options:\n",
   "  --version              -V         Print the version and quit.\n",
   "  --help                 -h         This text\n",
   "                         -C         Test config and stop.\n",
   "                         -e         Don't close stderr and stdout\n",
   "                         -d         Don't fork on daemonize\n",
   "  --verbosity <list>     -v <list>  List of flags.\n",
   "\n",
   "  --name <name>                     Set my name to this.\n",
   "  --ip <ip address>                 Set my IP to this.\n",
   "  --ifdev <string>                  Use IP of this ifdev.\n",
   "\n",
   "  --use_ccs              -c         Read config values from ccs.\n",
   "\n",
   "  --servers <list>       -s <list>  List of names or IPs.\n",
   "  --cluster_name <name>  -n <name>  The name of this cluster\n",
   "\n",
   "  --heartbeat_rate <time>           How often to check heartbeats\n",
   "  --allowed_misses <number>         How many concurrent missable\n",
   "  --new_connection_timeout <time>   How long to wait on new sockets\n",
   "  --master_scan_delay <time>        How long between probes.\n",
   "\n",
   "  --coreport <port>                 Which port to use.\n",
   "  --ltpxport <port>                 Which port to use.\n",
   "  --ltport <port>                   Which port to use.\n",
   "\n",
   "  --fence_bin <path>                full path to fence binary\n",
   "  --run_as <user>                   User to switch into\n",
   "  --lock_dir <path>                 Where are pid lock files kept\n",
   "  --lt_partitions <number>          How many Lock table partitions.\n",
   NULL
   };
   int i;
   for(i=0; strings[i] != NULL; i++)
      printf("%s", strings[i]);
   exit(ExitGulm_Usage);
}

enum {
   forgotten_long_opt = 0,
   coreport_opt = 1,
   ltpxport_opt,
   ltport_opt,
   hbr_opt,
   am_opt,
   nct_opt,
   msd_opt,
   fb_opt,
   ra_opt,
   ld_opt,
   lp_opt,
   lhl_opt,
   ldrr_opt,
   pal_opt,
   pah_opt,
   par_opt,
   name_opt,
   ip_opt,
   ifdev_opt
};
static struct option long_options[] = {
   {"version", 0, 0, 'V'},
   {"help", 0, 0, 'h'},

   {"use_ccs", 0, 0, 'c'},

   {"name", 1, 0, name_opt},
   {"ip", 1, 0, ip_opt},
   {"ifdev", 1, 0, ifdev_opt},

   {"coreport", 1, 0, coreport_opt},
   {"ltpxport", 1, 0, ltpxport_opt},
   {"ltport", 1, 0, ltport_opt},

   {"heartbeat_rate", 1, 0, hbr_opt},
   {"allowed_misses", 1, 0, am_opt},
   {"new_connection_timeout", 1, 0, nct_opt},
   {"master_scan_delay", 1, 0, msd_opt},

   {"verbosity", 1, 0, 'v'},
   {"servers", 1, 0, 's'},
   {"cluster_name", 1, 0, 'n'},

   {"fence_bin", 1, 0, fb_opt},
   {"run_as", 1, 0, ra_opt}, /* toss? */
   {"lock_dir", 1, 0, ld_opt},

   {"lt_partitions", 1, 0, lp_opt},

   {"lt_high_locks", 1, 0, lhl_opt}, /* toss? */
   {"lt_drop_req_rate", 1, 0, ldrr_opt}, /* toss? */

   {"prealloc_locks", 1, 0, pal_opt}, /* toss? */
   {"prealloc_holders", 1, 0, pah_opt}, /* toss? */
   {"prealloc_lkrqs", 1, 0, par_opt}, /* toss? */

   {0,0,0,0}
};
int parse_cmdline(gulm_config_t *gf, int argc, char **argv)
{
   int c;
   int option_index = 0;
   uint64_t temp;

   while(1) {

      c = getopt_long(argc, argv, "v:s:n:ecdCVh", long_options,&option_index);
      if( c == -1 ) break;

      switch(c) {
         case forgotten_long_opt:
            fprintf(stderr,"Bad programmer! You forgot to catch '%s' \n",
                    long_options[option_index].name);
            exit(ExitGulm_BadOption);
            break;
         case coreport_opt:
            gf->corePort = atoi(optarg);
            break;
         case ltpxport_opt:
            gf->ltpx_port = atoi(optarg);
            break;
         case ltport_opt:
            gf->lt_port = atoi(optarg);
            break;
         case hbr_opt:
            temp = ft2uint64(atof(optarg));
            gf->heartbeat_rate = bound_to_uint64(temp, 75000, (uint64_t)~0);
            /* min is 0.075 */
            break;
         case am_opt:
            gf->allowed_misses = bound_to_uint16(atoi(optarg), 1, 0xffff);
            break;
         case nct_opt:
            temp = ft2uint64(atof(optarg));
            gf->new_con_timeout = bound_to_uint64(temp, 0, (uint64_t)~0);
            /* min should be something bigger than zero...
             * say 0.5? why?
             */
            break;
         case msd_opt:
            temp = ft2uint64(atof(optarg));
            gf->master_scan_delay = bound_to_uint64(temp, 10, (uint64_t)~0);
            break;
         case fb_opt:
            strdup_with_free((char**)&gf->fencebin, optarg);
            break;
         case ra_opt:
            strdup_with_free((char**)&gf->run_as, optarg);
            break;
         case ld_opt:
            strdup_with_free((char**)&gf->lock_file, optarg);
            break;
         case lp_opt:
            gf->how_many_lts = bound_to_uint16(atoi(optarg), 1, 256);
            break;

         case lhl_opt:
            gf->lt_maxlocks = bound_to_ulong(atoi(optarg), 10000, ~0UL);
            break;
         case ldrr_opt:
            gf->lt_cf_rate = bound_to_uint(atoi(optarg), 5, ~0U);
            break;
         case pal_opt:
            gf->lt_prelocks = bound_to_uint(atoi(optarg), 0, ~0U);
            break;
         case pah_opt:
            gf->lt_preholds = bound_to_uint(atoi(optarg), 0, ~0U);
            break;
         case par_opt:
            gf->lt_prelkrqs = bound_to_uint(atoi(optarg), 0, ~0U);
            break;

         case name_opt:
            strdup_with_free((char**)&gf->name, optarg);
            break;
         case ip_opt:
            get_ip_for_name(optarg, &gf->ip);
            break;
         case ifdev_opt:
            strdup_with_free((char**)&gf->netdev, optarg);
            break;

         case 'c':
            parse_ccs(gf);
            break;
         case 'n':
            strdup_with_free((char**)&gf->clusterID, optarg);
            break;
         case 's':
            parse_cmdline_servers(gf, optarg);
           break;
         case 'v':
            set_verbosity(optarg, &verbosity);
            break;
         case 'C':
            gf->conf_test = TRUE;
            break;
         case 'e':
            gf->leave_std_open = TRUE;
            break;
         case 'd':
            gf->daemon_fork = FALSE;
            break;
         case 'V':
            printf("%s %s (built " __DATE__ " " __TIME__ ")\n"
                  "Copyright (C) 2004 Red Hat, Inc.  All rights reserved.\n",
                    ProgramName, RELEASE);
            exit(ExitGulm_Usage);
            break;
         case 'h':
            usage();
         case ':':
         case '?':
            fprintf(stderr, "Ambiguous options, see --help\n");
            exit(ExitGulm_BadOption);
         default:
            fprintf(stderr,"Bad programmer! You forgot to catch the %c flag\n",
                    c);
            exit(ExitGulm_BadOption);
            break;
      }

   }
   return 0;
}

/**
 * short_parse_conf - 
 * @argc: 
 * @argv: 
 * 
 * 
 * Returns: int
 */
int short_parse_conf(int argc, char **argv)
{
   int i;
   for(i=0; i < argc; i++) {
      if( strcmp("-h", argv[i]) == 0 ||
          strcmp("--help", argv[i]) == 0 ) {
            usage();
      }else
      if( strcmp("-V", argv[i]) == 0 ||
          strcmp("--version", argv[i]) == 0) {
            printf("%s %s (built " __DATE__ " " __TIME__ ")\n"
                  "Copyright (C) 2004 Red Hat, Inc.  All rights reserved.\n",
                    ProgramName, RELEASE);
            exit(ExitGulm_Usage);
      }
   }
   return 0;
}


