###############################################################################
###############################################################################
##
##  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
##  Copyright (C) 2004-2008 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

include ../make/defines.mk

SUBDIRS=libgfs gfs_debug gfs_edit gfs_fsck gfs_grow gfs_jadd gfs_mkfs gfs_quota gfs_tool man init.d

all: ${SUBDIRS}

%:
	set -e && \
	for i in ${SUBDIRS}; do \
		${MAKE} -C $$i $@; \
	done; \
	if [ "$@" = "install" ]; then \
		mkdir -p ${DESTDIR}/sbin; \
		cd ${DESTDIR}/sbin; \
			ln -sf mount.gfs2 mount.gfs; \
	fi; \
	if [ "$@" = "uninstall" ]; then \
		cd ${DESTDIR}/sbin; rm -f mount.gfs; \
	fi
