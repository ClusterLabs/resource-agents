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

#define EXPORT_SYMTAB
#define WANT_DEBUG_NAMES
#define WANT_GMALLOC_NAMES
#define EXTERN
#include "gulm.h"

#include <linux/init.h>

#include "util.h"
#include "gulm_procinfo.h"

MODULE_DESCRIPTION ("Grand Unified Locking Module " GULM_RELEASE_NAME);
MODULE_AUTHOR ("Red Hat, Inc.");
MODULE_LICENSE ("GPL");

extern gulm_cm_t gulm_cm;

/**
 * init_gulm - Initialize the gulm module
 *
 * Returns: 0 on success, -EXXX on failure
 */
int __init
init_gulm (void)
{
	int error;

	memset (&gulm_cm, 0, sizeof (gulm_cm_t));
	gulm_cm.loaded = FALSE;
	gulm_cm.hookup = NULL;

	/* register with the lm layers. */
	error = lm_register_proto (&gulm_ops);
	if (error)
		goto fail;

	error = init_proc_dir ();
	if (error != 0) {
		goto fail_lm;
	}

	init_gulm_fs ();

	printk ("Gulm %s (built %s %s) installed\n",
		GULM_RELEASE_NAME, __DATE__, __TIME__);

	return 0;

      fail_lm:
	lm_unregister_proto (&gulm_ops);

      fail:
	return error;
}

/**
 * exit_gulm - cleanup the gulm module
 *
 */

void __exit
exit_gulm (void)
{
	remove_proc_dir ();
	lm_unregister_proto (&gulm_ops);
}

module_init (init_gulm);
module_exit (exit_gulm);

/* the libgulm.h interface. */
EXPORT_SYMBOL (lg_initialize);
EXPORT_SYMBOL (lg_release);

EXPORT_SYMBOL (lg_core_handle_messages);
EXPORT_SYMBOL (lg_core_selector);
EXPORT_SYMBOL (lg_core_login);
EXPORT_SYMBOL (lg_core_logout);
EXPORT_SYMBOL (lg_core_nodeinfo);
EXPORT_SYMBOL (lg_core_nodelist);
EXPORT_SYMBOL (lg_core_servicelist);
EXPORT_SYMBOL (lg_core_corestate);
EXPORT_SYMBOL (lg_core_shutdown);
EXPORT_SYMBOL (lg_core_forceexpire);
EXPORT_SYMBOL (lg_core_forcepending);

EXPORT_SYMBOL (lg_lock_handle_messages);
EXPORT_SYMBOL (lg_lock_selector);
EXPORT_SYMBOL (lg_lock_login);
EXPORT_SYMBOL (lg_lock_logout);
EXPORT_SYMBOL (lg_lock_state_req);
EXPORT_SYMBOL (lg_lock_cancel_req);
EXPORT_SYMBOL (lg_lock_action_req);
EXPORT_SYMBOL (lg_lock_drop_exp);
