/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __DEBUG_DOT_H__
#define __DEBUG_DOT_H__

#ifdef DEBUG
#define ENTER(x) log_dbg("Entering %s\n", x)
#define EXIT(x) log_dbg("Exiting %s\n", x)
#else
#define ENTER(x)
#define EXIT(x)
#endif

#endif /* __DEBUG_DOT_H__ */
