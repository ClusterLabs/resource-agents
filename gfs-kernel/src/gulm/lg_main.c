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

/* This is where all of the library specific functions exist.
 * Not many, but keeps things clean.
 */

#include "lg_priv.h"
#include "gulm.h"
extern gulm_cm_t gulm_cm;

const struct in6_addr lg_in6addr_loopback = IN6ADDR_LOOPBACK_INIT;

/**
 * lg_initialize - 
 * @gulm_interface_p:
 * @cluster_name:
 * @service_name: 
 * 
 * if returning an error, nothing was done to the value of gulm_interface_p
 * 
 * Returns: gulm_interface_p
 */
int
lg_initialize (gulm_interface_p * ret, char *cluster_name, char *service_name)
{
	gulm_interface_t *lg;
	int err, len;

	lg = kmalloc (sizeof (gulm_interface_t), GFP_KERNEL);
	if (lg == NULL)
		return -ENOMEM;

	memset (lg, 0, sizeof (gulm_interface_t));
	lg->first_magic = LGMAGIC;
	lg->last_magic = LGMAGIC;

	if (cluster_name == NULL)
		cluster_name = "cluster";
	len = strlen (cluster_name) + 1;
	lg->clusterID = kmalloc (len, GFP_KERNEL);
	if (lg->clusterID == NULL) {
		err = -ENOMEM;
		goto fail_nomem;
	}
	memcpy (lg->clusterID, cluster_name, len);

	len = strlen (service_name) + 1;
	lg->service_name = kmalloc (len, GFP_KERNEL);
	if (lg->service_name == NULL) {
		err = -ENOMEM;
		goto fail_nomem;
	}
	memcpy (lg->service_name, service_name, len);

	/* set up flutter bufs. */
	lg->cfba_len = 64;
	lg->cfba = kmalloc (lg->cfba_len, GFP_KERNEL);
	if (lg->cfba == NULL) {
		err = -ENOMEM;
		goto fail_nomem;
	}

	lg->cfbb_len = 64;
	lg->cfbb = kmalloc (lg->cfbb_len, GFP_KERNEL);
	if (lg->cfbb == NULL) {
		err = -ENOMEM;
		goto fail_nomem;
	}

	lg->lfba_len = 128;
	lg->lfba = kmalloc (lg->lfba_len, GFP_KERNEL);
	if (lg->lfba == NULL) {
		err = -ENOMEM;
		goto fail_nomem;
	}

	lg->lfbb_len = 128;
	lg->lfbb = kmalloc (lg->lfbb_len, GFP_KERNEL);
	if (lg->lfbb == NULL) {
		err = -ENOMEM;
		goto fail_nomem;
	}

	/* setup mutexes */
	init_MUTEX (&lg->core_sender);
	init_MUTEX (&lg->core_recver);
	init_MUTEX (&lg->lock_sender);
	init_MUTEX (&lg->lock_recver);

	lg->core_port = 40040;
	lg->lock_port = 40042;

	*ret = lg;
	return 0;
      fail_nomem:
	if (lg->clusterID != NULL)
		kfree (lg->clusterID);
	if (lg->service_name != NULL)
		kfree (lg->service_name);
	if (lg->cfba != NULL)
		kfree (lg->cfba);
	if (lg->cfbb != NULL)
		kfree (lg->cfbb);
	if (lg->lfba != NULL)
		kfree (lg->lfba);
	if (lg->lfbb != NULL)
		kfree (lg->lfbb);
	kfree (lg);
	return err;
}

/**
 * lg_release - 
 * @lg: 
 * 
 */
void
lg_release (gulm_interface_p lgp)
{
	gulm_interface_t *lg = (gulm_interface_t *) lgp;
	if (lgp == NULL)
		return;
	/* make sure it is a gulm_interface_p. */
	if (lg->first_magic != LGMAGIC || lg->last_magic != LGMAGIC)
		return;

	if (lg->service_name != NULL)
		kfree (lg->service_name);
	if (lg->clusterID != NULL)
		kfree (lg->clusterID);

	/* wonder if I should send a logout packet? */
	if (lg->core_enc != NULL)
		xdr_enc_release (lg->core_enc);
	if (lg->core_dec != NULL)
		xdr_dec_release (lg->core_dec);
	xdr_close (&lg->core_fd);

	if (lg->lock_enc != NULL)
		xdr_enc_release (lg->lock_enc);
	if (lg->lock_dec != NULL)
		xdr_dec_release (lg->lock_dec);
	xdr_close (&lg->lock_fd);

	if (lg->cfba != NULL)
		kfree (lg->cfba);
	if (lg->cfbb != NULL)
		kfree (lg->cfbb);
	if (lg->lfba != NULL)
		kfree (lg->lfba);
	if (lg->lfbb != NULL)
		kfree (lg->lfbb);

	kfree (lg);
}

/**
 * lg_set_core_port - 
 * @lgp: 
 * @new: 
 * 
 * 
 * Returns: int
 */
int
lg_set_core_port (gulm_interface_p lgp, uint16_t new)
{
	gulm_interface_t *lg = (gulm_interface_t *) lgp;
	if (lgp == NULL)
		return -EINVAL;
	/* make sure it is a gulm_interface_p. */
	if (lg->first_magic != LGMAGIC || lg->last_magic != LGMAGIC)
		return -EINVAL;

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
int
lg_set_lock_port (gulm_interface_p lgp, uint16_t new)
{
	gulm_interface_t *lg = (gulm_interface_t *) lgp;
	if (lgp == NULL)
		return -EINVAL;
	/* make sure it is a gulm_interface_p. */
	if (lg->first_magic != LGMAGIC || lg->last_magic != LGMAGIC)
		return -EINVAL;

	lg->lock_port = new;

	return 0;
}
