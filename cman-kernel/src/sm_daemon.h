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

#ifndef __SM_DAEMON_DOT_H__
#define __SM_DAEMON_DOT_H__

#define DO_RUN                  (0)
#define DO_START_RECOVERY       (1)
#define DO_MESSAGES             (2)
#define DO_BARRIERS             (3)
#define DO_CALLBACKS            (4)
#define DO_JOINLEAVE            (5)
#define DO_RECOVERIES           (6)
#define DO_MEMBERSHIP           (7)
#define DO_RESET		(8)

void init_serviced(void);
void wake_serviced(int do_flag);
void stop_serviced(void);
int start_serviced(void);

#endif
