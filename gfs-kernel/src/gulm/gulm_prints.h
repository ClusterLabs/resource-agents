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

#ifndef __gulm_prints_h__
#define __gulm_prints_h__
#include "gulm_log_msg_bits.h"

#define PROTO_NAME "lock_gulm"

#ifdef GULM_ASSERT
#undef GULM_ASSERT
#endif
#define GULM_ASSERT(x, do) \
{ \
  if (!(x)) \
  { \
    printk("\n"PROTO_NAME":  Assertion failed on line %d of file %s\n" \
               PROTO_NAME":  assertion:  \"%s\"\n", \
               __LINE__, __FILE__, #x ); \
    {do} \
    panic("\n"PROTO_NAME":  Record message above and reboot.\n"); \
  } \
}

#define log_msg(v, fmt, args...) if(((v)&gulm_cm.verbosity)==(v)||(v)==lgm_Always) {\
   printk(PROTO_NAME ": " fmt, ## args); \
}
#define log_err(fmt, args...) {\
   printk(KERN_ERR PROTO_NAME ": ERROR " fmt, ## args); \
}

#define log_nop(fmt, args...)
#define TICK printk("TICK==>" PROTO_NAME ": [%s:%d] pid:%d\n" , __FILE__ , __LINE__ , current->pid )

#endif /*__gulm_prints_h__*/
