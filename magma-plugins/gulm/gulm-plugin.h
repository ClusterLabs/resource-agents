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
#ifndef _GULM_PLUGIN_H
#define _GULM_PLUGIN_H

#include <libgulm.h>

typedef struct {
	gulm_interface_p	interface;
	int			quorum_state;
	int			memb_count;
	uint64_t		memb_sum;
} gulm_priv_t;

struct nodelist_misc {
	char ret;
	cluster_member_list_t *members;
};

int gulm_lock_login(gulm_interface_p pg);
int gulm_lock_logout(gulm_interface_p pg);

int gulm_lock(cluster_plugin_t *self, char *resource, int flags,
	      void **lockpp);
int gulm_unlock(cluster_plugin_t *self, char *resource, void *lockp);


#endif /* _GULM_PLUGIN_H */
