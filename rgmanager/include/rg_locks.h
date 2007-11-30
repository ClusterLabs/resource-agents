/*
  Copyright Red Hat, Inc. 2004-2007

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
#ifndef __RG_LOCKS_H
#define __RG_LOCKS_H

int rg_running(void);

int rg_locked(void);
int rg_lockall(int flag);
int rg_unlockall(int flag);

int rg_quorate(void);
int rg_set_quorate(void);
int rg_set_inquorate(void);

int rg_inc_threads(void);
int rg_dec_threads(void);
int rg_wait_threads(void);

int rg_initialized(void);
int rg_set_initialized(void);
int rg_set_uninitialized(void);
int rg_wait_initialized(void);

int rg_inc_status(void);
int rg_dec_status(void);
int rg_set_statusmax(int max);

int ccs_lock(void);
int ccs_unlock(int fd);

#endif

