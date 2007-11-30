/*
  Copyright Red Hat, Inc. 2007

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License version 2 as published
  by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to the
  Free Software Foundation, Inc.,  675 Mass Ave, Cambridge, 
  MA 02139, USA.
*/
/* Time-based restart counters for rgmanager */

#ifndef _RESTART_COUNTER_H
#define _RESTART_COUNTER_H

typedef void *restart_counter_t;

int restart_add(restart_counter_t arg);
int restart_clear(restart_counter_t arg);
int restart_count(restart_counter_t arg);
restart_counter_t restart_init(time_t expire_timeout, int max_restarts);
int restart_cleanup(restart_counter_t arg);

#endif
