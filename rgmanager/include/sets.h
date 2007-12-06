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
/**
 @file sets.h - Header file for sets.c
 @author Lon Hohberger <lhh at redhat.com>
 */
#ifndef _SETS_H
#define _SETS_H

/* #include <stdint.h> */
typedef int set_type_t;

int s_add(set_type_t *, int *, set_type_t);
int s_union(set_type_t *, int, set_type_t *,
	    int, set_type_t **, int *);

int s_intersection(set_type_t *, int, set_type_t *,
		   int, set_type_t **, int *);
int s_delta(set_type_t *, int, set_type_t *,
	    int, set_type_t **, int *);
int s_subtract(set_type_t *, int, set_type_t *, int, set_type_t **, int *);
int s_shuffle(set_type_t *, int);

#endif
