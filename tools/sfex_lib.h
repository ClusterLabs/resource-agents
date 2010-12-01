/*-------------------------------------------------------------------------
 *
 * Shared Disk File EXclusiveness Control Program(SF-EX)
 *
 * sfex_lib.h --- Prototypes for lib.c.
 * 
 * Copyright (c) 2007 NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  
 * 02110-1301, USA.
 * 
 * $Id$
 *
 *-------------------------------------------------------------------------*/

#ifndef LIB_H
#define LIB_H

const char *get_progname(const char *argv0);
char *get_nodename(void);
void init_controldata(sfex_controldata *cdata, size_t blocksize, int numlocks);
void init_lockdata(sfex_lockdata *ldata);
void write_controldata(const sfex_controldata *cdata);
int write_lockdata(const sfex_controldata *cdata, const sfex_lockdata *ldata, int index);
int read_controldata(sfex_controldata *cdata);
int read_lockdata(const sfex_controldata *cdata, sfex_lockdata *ldata, int index);
int prepare_lock(const char *device);
int lock_index_check(sfex_controldata * cdata, int index);

#endif /* LIB_H */
