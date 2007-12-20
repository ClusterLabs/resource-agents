###############################################################################
###############################################################################
##
##  Copyright (C) 2007 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

include ../../make/defines.mk

TARGET3 = dlm_cleanup.3           dlm_lock_wait.3        dlm_open_lockspace.3    \
	  dlm_close_lockspace.3   dlm_ls_lock.3          dlm_pthread_init.3      \
	  dlm_create_lockspace.3  dlm_ls_lock_wait.3     dlm_release_lockspace.3 \
	  dlm_dispatch.3          dlm_ls_pthread_init.3  dlm_unlock.3		 \
	  dlm_get_fd.3            dlm_ls_unlock.3        dlm_unlock_wait.3	 \
	  dlm_lock.3              dlm_ls_unlock_wait.3   libdlm.3

TARGET8 = dlm_tool.8

all:

clean:

install:
	install -d ${mandir}/man3
	for i in ${TARGET3}; do \
	 install $(S)/$$i ${mandir}/man3; \
	done
	install -d ${mandir}/man8
	install $(S)/${TARGET8} ${mandir}/man8

uninstall:
	${UNINSTALL} ${TARGET3} ${mandir}/man3
	${UNINSTALL} ${TARGET8} ${mandir}/man8
