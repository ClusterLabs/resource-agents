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

/* all of the lock related fucntion are here. */
#include "lg_priv.h"

/**
 * lg_lock_selector - 
 * @ulm_interface_p: 
 * 
 * 
 * Returns: int
 */
xdr_socket lg_lock_selector(gulm_interface_p lgp)
{
   gulm_interface_t *lg = (gulm_interface_t *)lgp;
   /* make sure it is a gulm_interface_p. */
   if( lg == NULL || lg->first_magic != LGMAGIC || lg->last_magic != LGMAGIC )
#ifdef __KERNEL__
      return NULL;
#else
      return -EINVAL;
#endif

   return lg->lock_fd;
}

/**
 * lg_lock_handle_messages - 
 * @ulm_interface_p: 
 * @lg_lockspace_callbacks_t: 
 * 
 * Returns: int
 */
int lg_lock_handle_messages(gulm_interface_p lgp, lg_lockspace_callbacks_t* cbp,
      void *misc)
{
   gulm_interface_t *lg = (gulm_interface_t *)lgp;
   xdr_dec_t *dec;
   int err = 0;
   uint32_t x_code, x_error, x_flags;
   uint16_t x_keylen, x_lvblen=0;
   uint8_t x_state;

   /* make sure it is a gulm_interface_p. */
   if( lg == NULL ) return -EINVAL;
   if( lg->first_magic != LGMAGIC || lg->last_magic != LGMAGIC ) return -EINVAL;
   
   if( lg->core_enc == NULL || lg->core_dec == NULL) return -EBADR;

   lg_mutex_lock( &lg->lock_recver );
   if( lg->in_lock_hm ) return -EDEADLK;
   lg->in_lock_hm = TRUE;
   lg_mutex_unlock( &lg->lock_recver );

   dec = lg->lock_dec;

   err = xdr_dec_uint32(dec, &x_code);
   if( err != 0 ) goto exit;

   if( gulm_lock_login_rpl == x_code ) {
      do{
         if((err = xdr_dec_uint32(dec, &x_error)) != 0) break;
         if((err = xdr_dec_uint8(dec, &x_state)) != 0) break;
      }while(0);
      if( err != 0 ) goto exit;
      if( cbp->login_reply == NULL ) {err = 0; goto exit;}
      err = cbp->login_reply(misc, x_error, x_state);
      goto exit;
   }else
   if( gulm_lock_logout_rpl == x_code ) {
      if( cbp->logout_reply != NULL ) {
         err = cbp->logout_reply(misc);
      }

      xdr_close(&lg->lock_fd);
      xdr_enc_release(lg->lock_enc); lg->lock_enc = NULL;
      xdr_dec_release(lg->lock_dec); lg->lock_dec = NULL;

      goto exit;
   }else
   if( gulm_lock_state_rpl == x_code ) {
      do{
         if((err = xdr_dec_raw_ag(dec, (void**)&lg->lfba, &lg->lfba_len,
                     &x_keylen)) != 0) break;
         if((err = xdr_dec_uint8(dec, &x_state)) != 0) break;
         if((err = xdr_dec_uint32(dec, &x_flags)) != 0) break;
         if((err = xdr_dec_uint32(dec, &x_error)) != 0) break;
         if( x_flags & gio_lck_fg_hasLVB) {
            if((err = xdr_dec_raw_ag(dec, (void**)&lg->lfbb,
                        &lg->lfbb_len, &x_lvblen))!= 0) break;
         }
      }while(0);
      if( err != 0 ){
         goto exit;
      }
      if( x_keylen <= 4 ) {
         err = -EPROTO; /* or something */
         goto exit;
      }
      if( cbp->lock_state == NULL ) {
         err = 0;
         goto exit;
      }
      /* gio_lck_fg_hasLVB is an internal flag. so remove it before we show
       * the user what we got.
       */
      x_flags &= ~gio_lck_fg_hasLVB;
      err =  cbp->lock_state(misc, &lg->lfba[4], x_keylen-4,
                             x_state, x_flags, x_error, 
                             lg->lfbb, x_lvblen);
      goto exit;
   }else
   if( gulm_lock_action_rpl == x_code ) {
      do{
         if((err = xdr_dec_raw_ag(dec, (void**)&lg->lfba, &lg->lfba_len,
                     &x_keylen)) != 0) break;
         if((err = xdr_dec_uint8(dec, &x_state)) != 0) break;
         if((err = xdr_dec_uint32(dec, &x_error)) != 0) break;
      }while(0);
      if( err != 0 ) {
         goto exit;
      }
      if( x_keylen <= 4 ) {
         err = -EPROTO; /* or something */
         goto exit;
      }
      if( cbp->lock_action == NULL ) {
         err = 0;
         goto exit;
      }
         err = cbp->lock_action(misc, &lg->lfba[4], x_keylen-4, x_state,
                                x_error);
      goto exit;
   }else
   if( gulm_lock_cb_state == x_code ) {
      do{
         if((err = xdr_dec_raw_ag(dec, (void**)&lg->lfba, &lg->lfba_len,
                     &x_keylen)) != 0) break;
         if((err = xdr_dec_uint8(dec, &x_state)) != 0) break;
      }while(0);
      if( err != 0 ) {
         goto exit;
      }
      if( cbp->drop_lock_req == NULL ) {
         err = 0;
         goto exit;
      }
      err = cbp->drop_lock_req(misc, &lg->lfba[4], x_keylen-4, x_state);
      goto exit;
   }else
   if( gulm_lock_cb_dropall == x_code ) {
      if( cbp->drop_all == NULL ) {err = 0; goto exit;}
      err = cbp->drop_all(misc);
      goto exit;
   }else
   if( gulm_info_stats_rpl == x_code ) {
      if( cbp->status != NULL ) {
         if((err = cbp->status(misc, lglcb_start, NULL, NULL)) != 0)
            goto exit;
      }
      do{
         if((err = xdr_dec_list_start(dec)) != 0 ) break;
         while( xdr_dec_list_stop(dec) != 0 ) {
            if((err = xdr_dec_string_ag(dec, &lg->lfba, &lg->lfba_len)) != 0)
               break;
            if((err = xdr_dec_string_ag(dec, &lg->lfbb, &lg->lfbb_len)) != 0)
               break;
            if( cbp->status != NULL ) {
               if((err=cbp->status(misc, lglcb_item, lg->lfba, lg->lfbb))!=0){
                  break;
               }
            }
         }
      }while(0);
      if( err != 0 ) {
         goto exit;
      }
      if( cbp->status == NULL ) {err = 0; goto exit;}
      err = cbp->status(misc, lglcb_stop, NULL, NULL);
      goto exit;
   }else
   if( gulm_err_reply == x_code ) {
      do{
         if((err = xdr_dec_uint32(dec, &x_code)) != 0 ) break;
         if((err = xdr_dec_uint32(dec, &x_error)) != 0 ) break;
      }while(0);
      if( err != 0 ) goto exit;
      if( cbp->error == NULL ) {err = 0; goto exit;}
      err = cbp->error(misc, x_error);
      goto exit;
   }else
   {
      err = -EPROTO;
      goto exit;
   }

exit:
   lg->in_lock_hm = FALSE;
   return err;
}


/**
 * lg_lock_login - 
 * @ulm_interface_p: 
 * @4: 
 * 
 * 
 * Returns: int
 */
int lg_lock_login(gulm_interface_p lgp, uint8_t lockspace[4] )
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
   adr.sin6_port = htons(lg->lock_port);

   if( (err = xdr_open( &cfd )) < 0 ) {
      return err;
   }

   if( (err=xdr_connect(&adr, cfd))<0) {
      xdr_close(&cfd);
      return err;
   }

   enc = xdr_enc_init( cfd, 512 );
   if( enc == NULL ) {
      xdr_close( &cfd );
      return -ENOMEM;
   }

   dec = xdr_dec_init( cfd, 512 );
   if( enc == NULL ) {
      xdr_enc_release(enc);
      xdr_close( &cfd );
      return -ENOMEM;
   }

   do{
      if((err = xdr_enc_uint32(enc, gulm_lock_login_req))<0) break;
      if((err = xdr_enc_uint32(enc, GIO_WIREPROT_VERS))<0) break;
      if((err =xdr_enc_string(enc, lg->service_name))<0) break;
      if((err = xdr_enc_uint8(enc, gio_lck_st_Client))<0) break;
      if((err = xdr_enc_flush(enc))<0) break;

      if((err = xdr_enc_uint32(enc, gulm_lock_sel_lckspc))<0) break;
      if((err = xdr_enc_raw(enc, lockspace, 4))<0) break;
      /* don't flush here.
       * dumb programmer stunt.  This way, the lockspace selection won't
       * happen until the next thing the user of this lib sends.  Which
       * means it will be after we have received the login reply.
       *
       * Is there really a good reason not to flush here?
       */
   }while(0);
   if(err != 0 ) {
      xdr_dec_release(dec);
      xdr_enc_release(enc);
      xdr_close( &cfd );
      return err;
   }

   lg_mutex_lock( &lg->lock_sender );
   lg->lock_fd = cfd;
   lg->lock_enc = enc;
   lg->lock_dec = dec;

   memcpy(lg->lockspace, lockspace, 4);
   lg_mutex_unlock( &lg->lock_sender );

   return 0;
}

/**
 * lg_lock_logout - 
 * @ulm_interface_p: 
 * 
 * 
 * Returns: int
 */
int lg_lock_logout(gulm_interface_p lgp)
{
   gulm_interface_t *lg = (gulm_interface_t *)lgp;
   xdr_enc_t *enc;
   int err;

   /* make sure it is a gulm_interface_p. */
   if( lg == NULL ) return -EINVAL;
   if( lg->first_magic != LGMAGIC || lg->last_magic != LGMAGIC ) return -EINVAL;

   if( lg->lock_fd < 0 || lg->lock_enc == NULL || lg->lock_dec == NULL)
      return -EINVAL;

   enc = lg->lock_enc;

   lg_mutex_lock( &lg->lock_sender );
   do{
      if((err = xdr_enc_uint32(enc, gulm_lock_logout_req)) != 0 ) break;
      if((err = xdr_enc_flush(enc)) != 0 ) break;
   }while(0);
   lg_mutex_unlock( &lg->lock_sender );
   return err;
}

/**
 * lg_lock_state_req - 
 * @lgp: 
 * @key: 
 * @keylen: 
 * @state: 
 * @flags: 
 * @LVB: 
 * @LVBlen: 
 * 
 * 
 * Returns: int
 */
int lg_lock_state_req(gulm_interface_p lgp, uint8_t *key, uint16_t keylen,
      uint8_t state, uint32_t flags, uint8_t *LVB, uint16_t LVBlen)
{
   gulm_interface_t *lg = (gulm_interface_t *)lgp;
   struct iovec iov[2];
   xdr_enc_t *enc;
   uint32_t iflgs = 0;
   int err;

   /* make sure it is a gulm_interface_p. */
   if( lg == NULL ) return -EINVAL;
   if( lg->first_magic != LGMAGIC || lg->last_magic != LGMAGIC ) return -EINVAL;

   if( lg->lock_fd < 0 || lg->lock_enc == NULL || lg->lock_dec == NULL)
      return -EINVAL;

   if( state != lg_lock_state_Unlock &&
       state != lg_lock_state_Exclusive &&
       state != lg_lock_state_Deferred &&
       state != lg_lock_state_Shared )
      return -EINVAL;

   /* make sure only the accepted flags get passed through. */
   if( (flags & lg_lock_flag_DoCB ) ==  lg_lock_flag_DoCB)
      iflgs |= lg_lock_flag_DoCB;
   if( (flags & lg_lock_flag_Try ) == lg_lock_flag_Try )
      iflgs |= lg_lock_flag_Try;
   if( (flags & lg_lock_flag_Any ) == lg_lock_flag_Any )
      iflgs |= lg_lock_flag_Any;
   if( (flags & lg_lock_flag_IgnoreExp ) == lg_lock_flag_IgnoreExp )
      iflgs |= lg_lock_flag_IgnoreExp;
   if( (flags & lg_lock_flag_Piority ) == lg_lock_flag_Piority )
      iflgs |= lg_lock_flag_Piority;

   enc = lg->lock_enc;

   if( LVB != NULL && LVBlen > 0) iflgs |= gio_lck_fg_hasLVB;

   iov[0].iov_base = lg->lockspace;
   iov[0].iov_len = 4;
   iov[1].iov_base = key;
   iov[1].iov_len = keylen;

   lg_mutex_lock( &lg->lock_sender );
   do{
      if((err = xdr_enc_uint32(enc, gulm_lock_state_req)) != 0 ) break;
      if((err = xdr_enc_raw_iov(enc, 2, iov)) != 0 ) break;
      if((err = xdr_enc_uint8(enc, state)) != 0 ) break;
      if((err = xdr_enc_uint32(enc, iflgs)) != 0 ) break;
      if( iflgs & gio_lck_fg_hasLVB )
         if((err = xdr_enc_raw(enc, LVB, LVBlen)) != 0 ) break;
      if((err = xdr_enc_flush(enc)) != 0 ) break;
   }while(0);
   lg_mutex_unlock( &lg->lock_sender );
   return err;
}

/**
 * lg_lock_cancel_req - 
 * @lgp: 
 * @key: 
 * @keylen: 
 * 
 * 
 * Returns: int
 */
int lg_lock_cancel_req(gulm_interface_p lgp, uint8_t *key, uint16_t keylen)
{
   gulm_interface_t *lg = (gulm_interface_t *)lgp;
   struct iovec iov[2];
   xdr_enc_t *enc;
   int err;

   /* make sure it is a gulm_interface_p. */
   if( lg == NULL ) return -EINVAL;
   if( lg->first_magic != LGMAGIC || lg->last_magic != LGMAGIC ) return -EINVAL;

   if( lg->lock_fd < 0 || lg->lock_enc == NULL || lg->lock_dec == NULL)
      return -EINVAL;

   enc = lg->lock_enc;

   iov[0].iov_base = lg->lockspace;
   iov[0].iov_len = 4;
   iov[1].iov_base = key;
   iov[1].iov_len = keylen;

   lg_mutex_lock( &lg->lock_sender );
   do{
      if((err = xdr_enc_uint32(enc, gulm_lock_action_req)) != 0 ) break;
      if((err = xdr_enc_raw_iov(enc, 2, iov)) != 0 ) break;
      if((err = xdr_enc_uint8(enc, gio_lck_st_Cancel)) != 0 ) break;
      if((err = xdr_enc_flush(enc)) != 0 ) break;
   }while(0);
   lg_mutex_unlock( &lg->lock_sender );
   return err;
}

/**
 * lg_lock_action_req - 
 * @lgp: 
 * @key: 
 * @keylen: 
 * @action: 
 * @LVB: 
 * @LVBlen: 
 * 
 * XXX
 * I wonder if I should actually break this into three seperate calls for
 * the lvb stuff.  Does it really matter?
 * 
 * Returns: int
 */
int lg_lock_action_req(gulm_interface_p lgp, uint8_t *key, uint16_t keylen,
      uint8_t action, uint8_t *LVB, uint16_t LVBlen)
{
   gulm_interface_t *lg = (gulm_interface_t *)lgp;
   struct iovec iov[2];
   xdr_enc_t *enc;
   int err;

   /* make sure it is a gulm_interface_p. */
   if( lg == NULL ) return -EINVAL;
   if( lg->first_magic != LGMAGIC || lg->last_magic != LGMAGIC ) return -EINVAL;

   if( lg->lock_fd < 0 || lg->lock_enc == NULL || lg->lock_dec == NULL)
      return -EINVAL;

   if( action != lg_lock_act_HoldLVB &&
       action != lg_lock_act_UnHoldLVB &&
       action != lg_lock_act_SyncLVB )
      return -EINVAL;

   enc = lg->lock_enc;

   iov[0].iov_base = lg->lockspace;
   iov[0].iov_len = 4;
   iov[1].iov_base = key;
   iov[1].iov_len = keylen;

   lg_mutex_lock( &lg->lock_sender );
   do{
      if((err = xdr_enc_uint32(enc, gulm_lock_action_req)) != 0 ) break;
      if((err = xdr_enc_raw_iov(enc, 2, iov)) != 0 ) break;
      if((err = xdr_enc_uint8(enc, action)) != 0 ) break;
      if( action == gio_lck_st_SyncLVB)
         if((err = xdr_enc_raw(enc, LVB, LVBlen)) != 0 ) break;
      if((err = xdr_enc_flush(enc)) != 0 ) break;
   }while(0);
   lg_mutex_unlock( &lg->lock_sender );
   return err;
}

/**
 * lg_lock_drop_exp - 
 * @ulm_interface_p: 
 * @holder: 
 * @keymask: 
 * @kmlen: 
 * 
 * holder is the node name of the expired holder that you want to clear.
 * Only locks matching the keymask will be looked at. (most of the time you
 * will just set key to a bunch of 0xff to match all) The keymask lets you
 * basically subdivide your lockspace into smaller seperate parts.
 * (example, there is one gfs lockspace, but each filesystem gets its own
 * subpart of that larger space)
 *
 * If holder is NULL, all expired holders in your lockspace will get
 * dropped.
 * 
 * Returns: int
 */
int lg_lock_drop_exp(gulm_interface_p lgp, uint8_t *holder, uint8_t *key,
      uint16_t keylen)
{
   gulm_interface_t *lg = (gulm_interface_t *)lgp;
   struct iovec iov[2];
   xdr_enc_t *enc;
   int err;

   /* make sure it is a gulm_interface_p. */
   if( lg == NULL ) return -EINVAL;
   if( lg->first_magic != LGMAGIC || lg->last_magic != LGMAGIC ) return -EINVAL;

   if( lg->lock_fd < 0 || lg->lock_enc == NULL || lg->lock_dec == NULL)
      return -EINVAL;

   enc = lg->lock_enc;

   iov[0].iov_base = lg->lockspace;
   iov[0].iov_len = 4;
   iov[1].iov_base = key;
   iov[1].iov_len = (key!=NULL)?keylen:0;

   lg_mutex_lock( &lg->lock_sender );
   do{
      if((err = xdr_enc_uint32(enc, gulm_lock_drop_exp)) != 0 ) break;
      if((err = xdr_enc_string(enc, holder)) != 0 ) break;
      if((err = xdr_enc_raw_iov(enc, 2, iov)) != 0 ) break;
      if((err = xdr_enc_flush(enc)) != 0 ) break;
   }while(0);
   lg_mutex_unlock( &lg->lock_sender );
   return err;
}

/**
 * lg_lock_status - 
 * @lgp: 
 * 
 * 
 * Returns: int
 */
int lg_lock_status(gulm_interface_p lgp)
{
   gulm_interface_t *lg = (gulm_interface_t *)lgp;
   xdr_enc_t *enc;
   int err;

   /* make sure it is a gulm_interface_p. */
   if( lg == NULL ) return -EINVAL;
   if( lg->first_magic != LGMAGIC || lg->last_magic != LGMAGIC ) return -EINVAL;

   if( lg->lock_fd < 0 || lg->lock_enc == NULL || lg->lock_dec == NULL)
      return -EINVAL;

   enc = lg->lock_enc;

   lg_mutex_lock( &lg->lock_sender );
   do{
      if((err = xdr_enc_uint32(enc, gulm_info_stats_req)) != 0 ) break;
      if((err = xdr_enc_flush(enc)) != 0 ) break;
   }while(0);
   lg_mutex_unlock( &lg->lock_sender );
   return err;
}

/* vim: set ai cin et sw=3 ts=3 : */
