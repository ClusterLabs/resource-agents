/*
  Copyright Red Hat, Inc. 2004

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2, or (at your option) any
  later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to the
  Free Software Foundation, Inc.,  675 Mass Ave, Cambridge, 
  MA 02139, USA.
*/
#ifndef _RES_OCF_H
#define _RES_OCF_H 1

#define OCF_ROOT RESOURCE_ROOTDIR

#define OCF_API_VERSION "1.0"

#define OCF_RES_PREFIX "OCF_RESKEY_"

#define OCF_ROOT_STR "OCF_ROOT"
#define OCF_RA_VERSION_MAJOR_STR "OCF_RA_VERSION_MAJOR"
#define OCF_RA_VERSION_MINOR_STR "OCF_RA_VERSION_MINOR"
#define OCF_RESOURCE_INSTANCE_STR "OCF_RESOURCE_INSTANCE"
#define OCF_CHECK_LEVEL_STR "OCF_CHECK_LEVEL"
#define OCF_RESOURCE_TYPE_STR "OCF_RESOURCE_TYPE"

/*
   LSB return codes 
 */
#define OCF_RA_SUCCESS		0
#define OCF_RA_ERROR		1
#define OCF_RA_INVALID_ARG	2
#define OCF_RA_UNIMPLEMENTED	3
#define OCF_RA_PERMISSION	4
#define OCF_RA_NOT_INSTALLED	5
#define OCF_RA_NOT_CONFIGURED	6
#define OCF_RA_NOT_RUNNING	7
#define OCF_RA_MAX		7

#endif
