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
#include <pthread.h>
#include <magma.h>
#include <magmamsg.h>

#ifdef MDEBUG
#include <mallocdbg.h>
#endif

static pthread_rwlock_t memblock = PTHREAD_RWLOCK_INITIALIZER;
static uint64_t myid = NODE_ID_NONE;

static cluster_member_list_t *membership = NULL;

void
member_list_update(cluster_member_list_t *new_ml)
{
	pthread_rwlock_wrlock(&memblock);
	if (membership)
		cml_free(membership);
	if (new_ml)
		membership = cml_dup(new_ml);
	else
		membership = NULL;
	msg_update(membership);
	pthread_rwlock_unlock(&memblock);
}


cluster_member_list_t *
member_list(void)
{
	cluster_member_list_t *ret = NULL;
	pthread_rwlock_rdlock(&memblock);
	if (membership) 
		ret = cml_dup(membership);
	pthread_rwlock_unlock(&memblock);
	return ret;
}


uint64_t
my_id(void)
{
	uint64_t me;
	pthread_rwlock_rdlock(&memblock);
	me = myid;
	pthread_rwlock_unlock(&memblock);
	return me;
}


void
set_my_id(uint64_t me)
{
	pthread_rwlock_wrlock(&memblock);
	myid = me;
	pthread_rwlock_unlock(&memblock);
}
