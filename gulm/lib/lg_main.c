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

/* This is where all of the library specific functions exist.
 * Not many, but keeps things clean.
 */

#include "lg_priv.h"

/**
 * lg_initialize - 
 * @gulm_interface_p:
 * @service_name: 
 * 
 * if returning an error, nothing was done to the value of gulm_interface_p
 * 
 * Returns: gulm_interface_p
 */
int lg_initialize(gulm_interface_p *ret, char *cluster_name, char *service_name)
{
   gulm_interface_t *lg;
   int err, len;

   lg = lg_malloc(sizeof(gulm_interface_t));
   if( lg == NULL ) return -ENOMEM;

   memset(lg, 0, sizeof(gulm_interface_t));
   lg->first_magic = LGMAGIC;
   lg->last_magic = LGMAGIC;
   lg->core_fd = XDR_SOCKET_INIT;
   lg->lock_fd = XDR_SOCKET_INIT;

   if( cluster_name == NULL ) cluster_name = "";
   len = strlen(cluster_name) +1;
   lg->clusterID = lg_malloc(len);
   if( lg->clusterID == NULL ) {err = -ENOMEM; goto fail_nomem;}
   memcpy(lg->clusterID, cluster_name, len);

   len = strlen(service_name) +1;
   lg->service_name = lg_malloc(len);
   if( lg->service_name == NULL ) {err = -ENOMEM; goto fail_nomem;}
   memcpy(lg->service_name, service_name, len);

   /* set up flutter bufs. */
   lg->cfba_len = 64;
   lg->cfba = lg_malloc(lg->cfba_len);
   if( lg->cfba == NULL ) {err = -ENOMEM; goto fail_nomem;}

   lg->cfbb_len = 64;
   lg->cfbb = lg_malloc(lg->cfbb_len);
   if( lg->cfbb == NULL ) {err = -ENOMEM; goto fail_nomem;}

   lg->lfba_len = 128;
   lg->lfba = lg_malloc(lg->lfba_len);
   if( lg->lfba == NULL ) {err = -ENOMEM; goto fail_nomem;}

   lg->lfbb_len = 128;
   lg->lfbb = lg_malloc(lg->lfbb_len);
   if( lg->lfbb == NULL ) {err = -ENOMEM; goto fail_nomem;}

   /* setup mutexes */
   lg_mutex_init( &lg->core_sender );
   lg_mutex_init( &lg->core_recver );
   lg_mutex_init( &lg->lock_sender );
   lg_mutex_init( &lg->lock_recver );

   lg->core_port = 40040;
   lg->lock_port = 40042;

   *ret = lg;
   return 0;
fail_nomem:
   if( lg->clusterID != NULL ) lg_str_free(lg->clusterID);
   if( lg->service_name != NULL ) lg_str_free(lg->service_name);
   if( lg->cfba != NULL ) lg_free(lg->cfba, lg->cfba_len);
   if( lg->cfbb != NULL ) lg_free(lg->cfbb, lg->cfbb_len);
   if( lg->lfba != NULL ) lg_free(lg->lfba, lg->lfba_len);
   if( lg->lfbb != NULL ) lg_free(lg->lfbb, lg->lfbb_len);
   lg_free(lg, sizeof(gulm_interface_t));
   return err;
}


/**
 * lg_release - 
 * @lg: 
 * 
 */
void lg_release(gulm_interface_p lgp)
{
   gulm_interface_t *lg = (gulm_interface_t *)lgp;
   if( lgp == NULL ) return;
   /* make sure it is a gulm_interface_p. */
   if( lg->first_magic != LGMAGIC || lg->last_magic != LGMAGIC ) return;

   if( lg->service_name != NULL ) lg_str_free(lg->service_name);
   if( lg->clusterID != NULL ) lg_str_free(lg->clusterID);

   /* wonder if I should send a logout packet? */
   if( lg->core_enc != NULL ) xdr_enc_release(lg->core_enc);
   if( lg->core_dec != NULL ) xdr_dec_release(lg->core_dec);
   xdr_close(&lg->core_fd);

   if( lg->lock_enc != NULL ) xdr_enc_release(lg->lock_enc);
   if( lg->lock_dec != NULL ) xdr_dec_release(lg->lock_dec);
   xdr_close(&lg->lock_fd);

   if( lg->cfba != NULL ) lg_free(lg->cfba, lg->cfba_len);
   if( lg->cfbb != NULL ) lg_free(lg->cfbb, lg->cfbb_len);
   if( lg->lfba != NULL ) lg_free(lg->lfba, lg->lfba_len);
   if( lg->lfbb != NULL ) lg_free(lg->lfbb, lg->lfbb_len);

   lg_mutex_destroy( &lg->core_sender );
   lg_mutex_destroy( &lg->core_recver );
   lg_mutex_destroy( &lg->lock_sender );
   lg_mutex_destroy( &lg->lock_recver );

   lg_free(lg, sizeof(gulm_interface_t));
}

/**
 * lg_set_core_port - 
 * @lgp: 
 * @new: 
 * 
 * 
 * Returns: int
 */
int lg_set_core_port(gulm_interface_p lgp, uint16_t new)
{
   gulm_interface_t *lg = (gulm_interface_t *)lgp;
   if( lgp == NULL ) return -EINVAL;
   /* make sure it is a gulm_interface_p. */
   if( lg->first_magic != LGMAGIC || lg->last_magic != LGMAGIC ) return -EINVAL;

   lg->core_port = new;
   return 0;
}

/**
 * lg_set_ltpx_port - 
 * @lgp: 
 * @new: 
 * 
 * 
 * Returns: int
 */
int lg_set_lock_port(gulm_interface_p lgp, uint16_t new)
{
   gulm_interface_t *lg = (gulm_interface_t *)lgp;
   if( lgp == NULL ) return -EINVAL;
   /* make sure it is a gulm_interface_p. */
   if( lg->first_magic != LGMAGIC || lg->last_magic != LGMAGIC ) return -EINVAL;

   lg->lock_port = new;
   return 0;
}

/* vim: set ai cin et sw=3 ts=3 : */
