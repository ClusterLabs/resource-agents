/*
  Copyright Red Hat, Inc. 2004

  The Magma Cluster API Library is free software; you can redistribute
  it and/or modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either version
  2.1 of the License, or (at your option) any later version.

  The Magma Cluster API Library is distributed in the hope that it will
  be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
  of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
 */
/** @file
  Plugin header for SM plugin
 */
#ifndef __SM_PLUGIN_H
#define __SM_PLUGIN_H

#define SMS_NONE	0
#define SMS_JOINING	1
#define SMS_JOINED	2
#define SMS_LEAVING	3
#define SMS_LEFT	4

typedef struct {
	int	sockfd;
	int	quorum_state;
	int	memb_count;
	int     state;
	char    *groupname;
} sm_priv_t;

#endif /* __SM_PLUGIN_H */
