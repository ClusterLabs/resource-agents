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

#ifndef __config_priv_h__
#define __config_priv_h__
#include "config_gulm.h"
#include "LLi.h"
void release_node_list(LLi_t *list);
void strdup_with_free(char **dst, char *src);
uint16_t bound_to_uint16(int val, uint16_t min, uint16_t max);
unsigned int bound_to_uint(int val, unsigned int min, unsigned int max);
unsigned long bound_to_ulong(int val, unsigned long min, unsigned long max);
uint64_t bound_to_uint64(uint64_t val, uint64_t min, uint64_t max);
uint64_t ft2uint64(float time);

int parse_ccs(gulm_config_t *gf);
int parse_cmdline(gulm_config_t *gf, int argc, char **argv);
int verify_name_and_ip_ccs(char *name, uint32_t ip);
#endif /*__config_priv_h__*/

