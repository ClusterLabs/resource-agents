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
#ifndef __gulm_config_h__
#define __gulm_config_h__
#include "LLi.h"
#include <netinet/in.h>

/* I may want to add some kind of marker to every option that this holds.
 * A couple of bits to store weither or not items are different than the
 * default, and possibly other info.  Mostly if I work up the live config
 * modification changes that others have requested, it would be nice to be
 * able to dump a live config that only contained what was needed, and not
 * everything.
 *
 * I may be able to do this just by `diffing' against the defaults too.
 *
 * I should take the time and evaluate how much of this really truely needs
 * to be tightly tied into gulm.
 */

typedef struct {
   uint32_t hashval;/* hash of the config. */
   uint8_t *clusterID;
   uint8_t *fencebin;
   uint8_t *run_as;
   uint8_t *lock_file;

   uint16_t corePort;
   uint64_t heartbeat_rate; /* milli-sec res, ignore the micro-secs. */
   uint64_t master_scan_delay;
   uint64_t new_con_timeout;
   uint16_t allowed_misses;

   uint16_t quorum;

   uint16_t fog; /* true || false */

   uint16_t node_cnt;
   LLi_t node_list;

   uint16_t how_many_lts; /* int */
   uint16_t lt_port;
   uint16_t ltpx_port;

   unsigned int lt_cf_rate;
   unsigned long lt_maxlocks;
   unsigned int lt_hashbuckets;
   unsigned int lt_prelocks;
   unsigned int lt_prelkrqs;
   unsigned int lt_preholds;

   /* */
   int conf_test; /* stop after processing config? */
   int leave_std_open;
   int daemon_fork;

} gulm_config_t;


/* prototypes */
void release_gulm_config(gulm_config_t *gf);
void free_gulm_config(gulm_config_t *gf);
int rebuild_server_list(gulm_config_t *gf);
int parse_conf(gulm_config_t *gf, int argc, char **argv);
void dump_conf(gulm_config_t *gf, int out);
int serialize_config(gulm_config_t *gf, int fd);
int Can_I_be_a_master(gulm_config_t *gf, struct in6_addr *ip);
int get_lt_range(int which, int of, int *start, int *stop);
int verify_name_and_ip(char *name, struct in6_addr *ip);
#endif /*__gulm_config_h__*/
/* vim: set ai cin et sw=3 ts=3 : */
