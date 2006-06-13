/*
  Copyright Red Hat, Inc. 2004-2006

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
 * Locking.
 */
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <lock.h>
#include <sys/types.h>
#include <sys/select.h>
#include <pthread.h>


static void
ast_function(void * __attribute__ ((unused)) arg)
{
}


static int
wait_for_dlm_event(dlm_lshandle_t *ls)
{
	fd_set rfds;
	int fd = dlm_ls_get_fd(ls);

	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);

	if (select(fd + 1, &rfds, NULL, NULL, NULL) == 1)
		return dlm_dispatch(fd);

	return -1;
}


int
clu_lock(dlm_lshandle_t ls,
	 int mode,
	 struct dlm_lksb *lksb,
	 int options,
         char *resource)
{
        int ret;

	if (!ls || !lksb || !resource || !strlen(resource)) {
		errno = EINVAL;
		return -1;
	}

        ret = dlm_ls_lock(ls, mode, lksb, options, resource,
                          strlen(resource), 0, ast_function, lksb,
                          NULL, NULL);

        if (ret < 0) {
                if (errno == ENOENT)
                        assert(0);

                return -1;
        }

        if ((ret = (wait_for_dlm_event(ls) < 0))) {
                fprintf(stderr, "wait_for_dlm_event: %d / %d\n",
                        ret, errno);
                return -1;
        }

        return 0;
}



dlm_lshandle_t
clu_acquire_lockspace(const char *lsname)
{
        dlm_lshandle_t ls = NULL;

        while (!ls) {
                ls = dlm_open_lockspace(lsname);
                if (ls)
                        break;

                ls = dlm_create_lockspace(lsname, 0644);
                if (ls)
                        break;

                /* Work around race: Someone was closing lockspace as
                   we were trying to open it.  Retry. */
                if (errno == ENOENT)
                        continue;

                fprintf(stderr, "failed acquiring lockspace: %s\n",
                        strerror(errno));

                return NULL;
        }

        return ls;
}



int
clu_unlock(dlm_lshandle_t ls, struct dlm_lksb *lksb)
{
        int ret;

	if (!ls || !lksb) {
		errno = EINVAL;
		return -1;
	}

        ret = dlm_ls_unlock(ls, lksb->sb_lkid, 0, lksb, NULL);

        if (ret != 0)
                return ret;

        /* lksb->sb_status should be EINPROG at this point */

        if (wait_for_dlm_event(ls) < 0) {
                errno = lksb->sb_status;
                return -1;
        }

        return 0;
}


int
clu_release_lockspace(dlm_lshandle_t ls, char *name)
{
        return dlm_release_lockspace(name, ls, 0);
}
