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

/* All of the core related functions for services are here. */

#include "lg_priv.h"


/**
 * lg_core_selector - 
 * @ulm_interface_p: 
 * 
 * 
 * Returns: int
 */
xdr_socket lg_core_selector(gulm_interface_p lgp)
{
   gulm_interface_t *lg = (gulm_interface_t *)lgp;
   /* make sure it is a gulm_interface_p. */
   if( lg == NULL || lg->first_magic != LGMAGIC || lg->last_magic != LGMAGIC )
#ifdef __KERNEL__
      return NULL;
#else
      return -EINVAL;
#endif

   return lg->core_fd;
}

/**
 * lg_core_handle_messages - 
 * @ulm_interface_p: 
 * @lg_core_callbacks_t: 
 * 
 * 
 * Returns: int
 */
int lg_core_handle_messages(gulm_interface_p lgp, lg_core_callbacks_t* ccbp,
      void *misc)
{
   gulm_interface_t *lg = (gulm_interface_t *)lgp;
   xdr_dec_t *dec;
   int err = 0;
   uint64_t x_gen;
   uint32_t x_code, x_error, x_rank;
   struct in6_addr x_ip;
   uint8_t x_state, x_mode;

   /* make sure it is a gulm_interface_p. */
   if( lg == NULL ) return -EINVAL;
   if( lg->first_magic != LGMAGIC || lg->last_magic != LGMAGIC ) return -EINVAL;
   
   if( lg->core_enc == NULL || lg->core_dec == NULL) return -EBADR;

   lg_mutex_lock( &lg->core_recver );
   if( lg->in_core_hm ) return -EDEADLK;
   lg->in_core_hm = TRUE;
   lg_mutex_unlock( &lg->core_recver );

   dec = lg->core_dec;

   err = xdr_dec_uint32(dec, &x_code);
   if( err != 0 ) goto exit;

   if( gulm_core_login_rpl == x_code ) {
      do{
         if((err = xdr_dec_uint64(dec, &x_gen))<0) break;
         if((err = xdr_dec_uint32(dec, &x_error))<0) break;
         if((err = xdr_dec_uint32(dec, &x_rank))<0) break;
         if((err = xdr_dec_uint8(dec, &x_state))<0) break;
      }while(0);
      if( err != 0 ) goto exit;
      if( ccbp->login_reply == NULL ) {err=0; goto exit;}
      err = ccbp->login_reply(misc, x_gen, x_error, x_rank, x_state);
      goto exit;
   }else
   if( gulm_core_logout_rpl == x_code ) {
      if((err = xdr_dec_uint32(dec, &x_error)) != 0 ) goto exit;
      if( ccbp->logout_reply != NULL ) {
         err = ccbp->logout_reply(misc);
      }

      xdr_close(&lg->core_fd);
      xdr_enc_release(lg->core_enc); lg->core_enc = NULL;
      xdr_dec_release(lg->core_dec); lg->core_dec = NULL;

      goto exit;
   }else
   if( gulm_core_mbr_lstrpl == x_code ) {
      if( ccbp->nodelist != NULL ) {
         err = ccbp->nodelist(misc, lglcb_start, NULL, 0, 0);
         if( err != 0 ) goto exit;
      }
      do{
         if((err = xdr_dec_list_start(dec)) != 0 ) break;
         while( xdr_dec_list_stop(dec) != 0 ) {
            if((err = xdr_dec_string_ag(dec, &lg->cfba, &lg->cfba_len)) != 0 )
               break;
            if((err = xdr_dec_ipv6(dec, &x_ip)) != 0 ) break;
            if((err = xdr_dec_uint8(dec, &x_state)) != 0 ) break;
            if((err = xdr_dec_uint8(dec, &x_mode)) != 0 ) break; /* skip over this... */
            if((err = xdr_dec_uint8(dec, &x_mode)) != 0 ) break; /* skip over this... */
            if((err = xdr_dec_uint32(dec, &x_rank)) != 0 ) break; /* skip over this... */
            if((err = xdr_dec_uint64(dec, &x_gen)) != 0 ) break; /* skip over this... */
            if((err = xdr_dec_uint64(dec, &x_gen)) != 0 ) break; /* skip over this... */
            if((err = xdr_dec_uint64(dec, &x_gen)) != 0 ) break; /* skip over this... */

            if( ccbp->nodelist != NULL ) {
               err = ccbp->nodelist(misc, lglcb_item, lg->cfba, &x_ip, x_state);
               if( err != 0 ) goto exit;
            }

         }
      }while(0);
      if( err != 0 ) {
         goto exit;
      }
      if( ccbp->nodelist == NULL ) {err=0; goto exit;}
      err = ccbp->nodelist(misc, lglcb_stop, NULL, 0, 0);
      goto exit;
   }else
   if( gulm_core_state_chgs == x_code ) {
      do{
         if((err = xdr_dec_uint8(dec, &x_state)) != 0 ) break;
         if( x_state == gio_Mbr_ama_Slave ) {
            if((err = xdr_dec_ipv6(dec, &x_ip)) != 0 ) break;
            if((err = xdr_dec_string_ag(dec, &lg->cfba, &lg->cfba_len)) != 0 )
               break;
         }
      }while(0);
      if( err != 0 ) {
         goto exit;
      }
      if( ccbp->statechange == NULL ) {
         err = 0;
         goto exit;
      }
      err = ccbp->statechange(misc, x_state, &x_ip, lg->cfba);
      goto exit;
   }else
   if( gulm_core_mbr_updt == x_code ) {
      do{
         if((err = xdr_dec_string_ag(dec, &lg->cfba, &lg->cfba_len)) != 0 )
            break;
         if((err = xdr_dec_ipv6(dec, &x_ip)) != 0) break;
         if((err = xdr_dec_uint8(dec, &x_state)) != 0 ) break;
      }while(0);
      if( err != 0 ) {
         goto exit;
      }
      if( ccbp->nodechange == NULL ) {
         err = 0;
         goto exit;
      }
      err = ccbp->nodechange(misc, lg->cfba, &x_ip, x_state); 
      goto exit;
   }else
   if( gulm_core_res_list == x_code ) {
      if( ccbp->service_list != NULL ) {
         if((err = ccbp->service_list(misc, lglcb_start, NULL)) != 0 )
            goto exit;
      }
      do{
         if((err = xdr_dec_list_start(dec)) != 0) break;
         while( xdr_dec_list_stop(dec) ) {
            if((err = xdr_dec_string_ag(dec, &lg->cfba, &lg->cfba_len)) != 0)
               break;
            if( ccbp->service_list != NULL ) {
               if((err = ccbp->service_list(misc, lglcb_item, lg->cfba)) != 0){
                  goto exit;
               }
            }
         }
      }while(0);
      if( err != 0 ) {
         goto exit;
      }
      if( ccbp->service_list == NULL ) {
         err = 0;
         goto exit;
      }
      err = ccbp->service_list(misc, lglcb_stop, NULL);
      goto exit;
   }else
   if( gulm_info_stats_rpl == x_code ) {
      do{
         if((err = xdr_dec_list_start(dec)) != 0 ) break;
         while( xdr_dec_list_stop(dec) != 0 ) {
            if((err = xdr_dec_string_ag(dec, &lg->cfba, &lg->cfba_len)) != 0)
               break;
            if((err = xdr_dec_string_ag(dec, &lg->cfbb, &lg->cfbb_len)) != 0)
               break;
         }
      }while(0);
      goto exit;
   }else
   if( gulm_err_reply == x_code ) {
      if((err = xdr_dec_uint32(dec, &x_code)) != 0 ) goto exit;
      if((err = xdr_dec_uint32(dec, &x_error)) != 0 ) goto exit;
      if( ccbp->error == NULL ) {err = 0; goto exit;}
      err = ccbp->error(misc, x_error);
      goto exit;
   }else
   {
      /* unknown code. what to do? */
      err = -EPROTO;
      goto exit;
   }

exit:
   lg->in_core_hm = FALSE;
   return err;
}

/**
 * lg_core_login - 
 * @lgp: 
 * @important: 
 *
 * On any error, things are closed and released to the state of things
 * before you called login.
 * 
 * Returns: int
 */
int lg_core_login(gulm_interface_p lgp, int important)
{
   gulm_interface_t *lg = (gulm_interface_t *)lgp;
   struct sockaddr_in6 adr;
   int err;
   xdr_socket cfd;
   xdr_enc_t *enc;
   xdr_dec_t *dec;

   /* make sure it is a gulm_interface_p. */
   if( lg == NULL ) return -EINVAL;
   if( lg->first_magic != LGMAGIC || lg->last_magic != LGMAGIC ) return -EINVAL;

   adr.sin6_family = AF_INET6;
   adr.sin6_addr = in6addr_loopback;
   adr.sin6_port = htons(lg->core_port);

   if( (err=xdr_open( &cfd ) ) <0) {
      return err;
   }

   if( (err=xdr_connect(&adr, cfd))<0) {
      xdr_close(&cfd);
      return err;
   }

   enc = xdr_enc_init( cfd, 128 );
   if( enc == NULL ) {
      xdr_close( &cfd );
      return -ENOMEM;
   }

   dec = xdr_dec_init( cfd, 128 );
   if( enc == NULL ) {
      xdr_enc_release(enc);
      xdr_close( &cfd );
      return -ENOMEM;
   }

   do{
      if((err = xdr_enc_uint32(enc, gulm_core_reslgn_req))<0) break;
      if((err = xdr_enc_uint32(enc, GIO_WIREPROT_VERS))<0) break;
      if((err = xdr_enc_string(enc, lg->clusterID))<0) break;
      if((err = xdr_enc_string(enc, lg->service_name))<0) break;
      if((err = xdr_enc_uint32(enc, important?gulm_svc_opt_important:0)) != 0 )
         break;
      if((err = xdr_enc_flush(enc))<0) break;
   }while(0);
   if(err != 0 ) {
      xdr_dec_release(dec);
      xdr_enc_release(enc);
      xdr_close( &cfd );
      return err;
   }

   lg_mutex_lock( &lg->core_sender );
   lg->core_fd = cfd;
   lg->core_enc = enc;
   lg->core_dec = dec;
   lg_mutex_unlock( &lg->core_sender );

   return 0;
}

/**
 * lg_core_logout - 
 * @lgp: 
 * 
 * 
 * Returns: int
 */
int lg_core_logout(gulm_interface_p lgp)
{
   gulm_interface_t *lg = (gulm_interface_t *)lgp;
   xdr_enc_t *enc;
   int err;

   /* make sure it is a gulm_interface_p. */
   if( lg == NULL ) return -EINVAL;
   if( lg->first_magic != LGMAGIC || lg->last_magic != LGMAGIC ) return -EINVAL;

   if( lg->core_fd < 0 || lg->core_enc == NULL || lg->core_dec == NULL)
      return -EINVAL;

   enc = lg->core_enc;

   lg_mutex_lock( &lg->core_sender );
   do{
      if((err = xdr_enc_uint32(enc, gulm_core_logout_req))!= 0 ) break;
      if((err = xdr_enc_string(enc, lg->service_name))!= 0 ) break;
      if((err = xdr_enc_uint8(enc, gio_Mbr_ama_Resource))!= 0 ) break;
      if((err = xdr_enc_flush(enc)) != 0 ) break;
   }while(0);
   lg_mutex_unlock( &lg->core_sender );
   return err;
}

/**
 * lg_core_nodeinfo - 
 * @lgp: 
 * @nodename: 
 * 
 * 
 * Returns: int
 */
int lg_core_nodeinfo(gulm_interface_p lgp, char *nodename)
{
   gulm_interface_t *lg = (gulm_interface_t *)lgp;
   xdr_enc_t *enc;
   int err;

   /* make sure it is a gulm_interface_p. */
   if( lg == NULL ) return -EINVAL;
   if( lg->first_magic != LGMAGIC || lg->last_magic != LGMAGIC ) return -EINVAL;

   if( lg->core_fd < 0 || lg->core_enc == NULL || lg->core_dec == NULL)
      return -EINVAL;

   if( nodename == NULL ) return -EINVAL;

   enc = lg->core_enc;

   lg_mutex_lock( &lg->core_sender );
   do{
      if((err = xdr_enc_uint32(enc, gulm_core_mbr_req)) != 0 ) break;
      if((err = xdr_enc_string(enc, nodename)) != 0 ) break;
      if((err = xdr_enc_flush(enc)) != 0 ) break;
   }while(0);
   lg_mutex_unlock( &lg->core_sender );
   return err;
}

/**
 * lg_core_nodelist - 
 * @lgp: 
 * 
 * 
 * Returns: int
 */
int lg_core_nodelist(gulm_interface_p lgp)
{
   gulm_interface_t *lg = (gulm_interface_t *)lgp;
   xdr_enc_t *enc;
   int err;

   /* make sure it is a gulm_interface_p. */
   if( lg == NULL ) return -EINVAL;
   if( lg->first_magic != LGMAGIC || lg->last_magic != LGMAGIC ) return -EINVAL;

   if( lg->core_fd < 0 || lg->core_enc == NULL || lg->core_dec == NULL)
      return -EINVAL;

   enc = lg->core_enc;

   lg_mutex_lock( &lg->core_sender );
   do{
      if((err = xdr_enc_uint32(enc, gulm_core_mbr_lstreq)) != 0 ) break;
      if((err = xdr_enc_flush(enc)) != 0 ) break;
   }while(0);
   lg_mutex_unlock( &lg->core_sender );
   return err;
}

/**
 * lg_core_servicelist - 
 * @lgp: 
 * 
 * 
 * Returns: int
 */
int lg_core_servicelist(gulm_interface_p lgp)
{
   gulm_interface_t *lg = (gulm_interface_t *)lgp;
   xdr_enc_t *enc;
   int err;

   /* make sure it is a gulm_interface_p. */
   if( lg == NULL ) return -EINVAL;
   if( lg->first_magic != LGMAGIC || lg->last_magic != LGMAGIC ) return -EINVAL;

   if( lg->core_fd < 0 || lg->core_enc == NULL || lg->core_dec == NULL)
      return -EINVAL;

   enc = lg->core_enc;

   lg_mutex_lock( &lg->core_sender );
   do{
      if((err = xdr_enc_uint32(enc, gulm_core_res_req)) != 0 ) break;
      if((err = xdr_enc_flush(enc)) != 0 ) break;
   }while(0);
   lg_mutex_unlock( &lg->core_sender );
   return err;
}

/**
 * lg_core_corestate - 
 * @lgp: 
 * 
 * 
 * Returns: int
 */
int lg_core_corestate(gulm_interface_p lgp)
{
   gulm_interface_t *lg = (gulm_interface_t *)lgp;
   xdr_enc_t *enc;
   int err;

   /* make sure it is a gulm_interface_p. */
   if( lg == NULL ) return -EINVAL;
   if( lg->first_magic != LGMAGIC || lg->last_magic != LGMAGIC ) return -EINVAL;

   if( lg->core_fd < 0 || lg->core_enc == NULL || lg->core_dec == NULL)
      return -EINVAL;

   enc = lg->core_enc;

   lg_mutex_lock( &lg->core_sender );
   do{
      if((err = xdr_enc_uint32(enc, gulm_core_state_req)) != 0 ) break;
      if((err = xdr_enc_flush(enc)) != 0 ) break;
   }while(0);
   lg_mutex_unlock( &lg->core_sender );
   return err;
}


/**
 * lg_core_shutdown - 
 * @lgp: 
 * 
 * 
 * Returns: int
 */
int lg_core_shutdown(gulm_interface_p lgp)
{
   gulm_interface_t *lg = (gulm_interface_t *)lgp;
   xdr_enc_t *enc;
   int err;

   /* make sure it is a gulm_interface_p. */
   if( lg == NULL ) return -EINVAL;
   if( lg->first_magic != LGMAGIC || lg->last_magic != LGMAGIC ) return -EINVAL;

   if( lg->core_fd < 0 || lg->core_enc == NULL || lg->core_dec == NULL)
      return -EINVAL;

   enc = lg->core_enc;

   lg_mutex_lock( &lg->core_sender );
   do{
      if((err = xdr_enc_uint32(enc, gulm_core_shutdown)) != 0 ) break;
      if((err = xdr_enc_flush(enc)) != 0 ) break;
   }while(0);
   lg_mutex_unlock( &lg->core_sender );
   return err;
}

/**
 * lg_core_forceexpire - 
 * @lgp: 
 * @node_name: 
 * 
 * 
 * Returns: int
 */
int lg_core_forceexpire(gulm_interface_p lgp, char *nodename)
{
   gulm_interface_t *lg = (gulm_interface_t *)lgp;
   xdr_enc_t *enc;
   int err;

   /* make sure it is a gulm_interface_p. */
   if( lg == NULL ) return -EINVAL;
   if( lg->first_magic != LGMAGIC || lg->last_magic != LGMAGIC ) return -EINVAL;

   if( lg->core_fd < 0 || lg->core_enc == NULL || lg->core_dec == NULL)
      return -EINVAL;

   if( nodename == NULL ) return -EINVAL;

   enc = lg->core_enc;

   lg_mutex_lock( &lg->core_sender );
   do{
      if((err = xdr_enc_uint32(enc, gulm_core_mbr_force)) != 0 ) break;
      if((err = xdr_enc_string(enc, nodename)) != 0 ) break;
      if((err = xdr_enc_flush(enc)) != 0 ) break;
   }while(0);
   lg_mutex_unlock( &lg->core_sender );
   return err;
}

/**
 * lg_core_forcepending - 
 * @lgp: 
 * 
 * 
 * Returns: int
 */
int lg_core_forcepending(gulm_interface_p lgp)
{
   gulm_interface_t *lg = (gulm_interface_t *)lgp;
   xdr_enc_t *enc;
   int err;

   /* make sure it is a gulm_interface_p. */
   if( lg == NULL ) return -EINVAL;
   if( lg->first_magic != LGMAGIC || lg->last_magic != LGMAGIC ) return -EINVAL;

   if( lg->core_fd < 0 || lg->core_enc == NULL || lg->core_dec == NULL)
      return -EINVAL;

   enc = lg->core_enc;

   lg_mutex_lock( &lg->core_sender );
   do{
      if((err = xdr_enc_uint32(enc, gulm_core_forcepend)) != 0 ) break;
      if((err = xdr_enc_flush(enc)) != 0 ) break;
   }while(0);
   lg_mutex_unlock( &lg->core_sender );
   return err;
}


/* vim: set ai cin et sw=3 ts=3 : */
