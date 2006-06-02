#ifndef _LOCK_H
#define _LOCK_H

#include <stdint.h>
#include <libdlm.h>

int clu_lock(dlm_lshandle_t ls, int mode, struct dlm_lksb *lksb,
	     int options, char *resource);
dlm_lshandle_t clu_acquire_lockspace(const char *lsname);
int clu_unlock(dlm_lshandle_t ls, struct dlm_lksb *lksb);
int clu_release_lockspace(dlm_lshandle_t ls, char *lsname);

#endif
