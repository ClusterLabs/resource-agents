#ifndef _LOCK_H
#define _LOCK_H

#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>
#include <libdlm.h>

int clu_ls_lock(dlm_lshandle_t ls, int mode, struct dlm_lksb *lksb,
	     	int options, char *resource);
dlm_lshandle_t clu_open_lockspace(const char *lsname);
int clu_ls_unlock(dlm_lshandle_t ls, struct dlm_lksb *lksb);
int clu_close_lockspace(dlm_lshandle_t ls, const char *lsname);

/* Default lockspace wrappers */
int clu_lock_init(const char *default_lsname);
int clu_lock(int mode, struct dlm_lksb *lksb,
	     int options, char *resource);
int clu_unlock(struct dlm_lksb *lksb);
void clu_lock_finished(const char *default_lsname);

#endif
