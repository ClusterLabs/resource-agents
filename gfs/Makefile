###############################################################################
###############################################################################
##
##  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
##  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

all:
	${MAKE} -C libgfs all
	${MAKE} -C gfs_debug all
	${MAKE} -C gfs_edit all
	${MAKE} -C gfs_fsck all
	${MAKE} -C gfs_grow all
	${MAKE} -C gfs_jadd all
	${MAKE} -C gfs_mkfs all
	${MAKE} -C gfs_quota all
	${MAKE} -C gfs_tool all

clean:
	${MAKE} -C libgfs clean
	${MAKE} -C gfs_debug clean
	${MAKE} -C gfs_edit clean
	${MAKE} -C gfs_fsck clean
	${MAKE} -C gfs_grow clean
	${MAKE} -C gfs_jadd clean
	${MAKE} -C gfs_mkfs clean
	${MAKE} -C gfs_quota clean
	${MAKE} -C gfs_tool clean

distclean: clean
	rm -f make/defines.mk

install: all
	${MAKE} -C libgfs install
	${MAKE} -C gfs_debug install
	${MAKE} -C gfs_edit install
	${MAKE} -C gfs_fsck install
	${MAKE} -C gfs_grow install
	${MAKE} -C gfs_jadd install
	${MAKE} -C gfs_mkfs install
	${MAKE} -C gfs_quota install
	${MAKE} -C gfs_tool install
	${MAKE} -C man install
	${MAKE} -C init.d install

deinstall uninstall:
	${MAKE} -C libgfs uninstall
	${MAKE} -C gfs_debug uninstall
	${MAKE} -C gfs_edit uninstall
	${MAKE} -C gfs_fsck uninstall
	${MAKE} -C gfs_grow uninstall
	${MAKE} -C gfs_jadd uninstall
	${MAKE} -C gfs_mkfs uninstall
	${MAKE} -C gfs_quota uninstall
	${MAKE} -C gfs_tool uninstall
	${MAKE} -C man uninstall
	${MAKE} -C init.d uninstall
