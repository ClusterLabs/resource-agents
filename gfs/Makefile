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
	cd libgfs && ${MAKE} all
	cd gfs_debug && ${MAKE} all
	cd gfs_edit && ${MAKE} all
	cd gfs_fsck && ${MAKE} all
	cd gfs_grow && ${MAKE} all
	cd gfs_jadd && ${MAKE} all
	cd gfs_mkfs && ${MAKE} all
	cd gfs_quota && ${MAKE} all
	cd gfs_tool && ${MAKE} all

copytobin:
	cd gfs_debug && ${MAKE} copytobin
	cd gfs_debug && ${MAKE} copytobin
	cd gfs_edit && ${MAKE} copytobin
	cd gfs_fsck && ${MAKE} copytobin
	cd gfs_grow && ${MAKE} copytobin
	cd gfs_jadd && ${MAKE} copytobin
	cd gfs_mkfs && ${MAKE} copytobin
	cd gfs_quota && ${MAKE} copytobin
	cd gfs_tool && ${MAKE} copytobin

clean:
	cd bin && ${MAKE} clean
	cd libgfs && ${MAKE} clean
	cd gfs_debug && ${MAKE} clean
	cd gfs_edit && ${MAKE} clean
	cd gfs_fsck && ${MAKE} clean
	cd gfs_grow && ${MAKE} clean
	cd gfs_jadd && ${MAKE} clean
	cd gfs_mkfs && ${MAKE} clean
	cd gfs_quota && ${MAKE} clean
	cd gfs_tool && ${MAKE} clean

distclean: clean
	rm -f make/defines.mk

install:
	cd bin && ${MAKE} install
	cd man && ${MAKE} install
	cd init.d && ${MAKE} install

deinstall uninstall:
	cd bin && ${MAKE} uninstall
	cd man && ${MAKE} uninstall
	cd init.d && ${MAKE} uninstall

