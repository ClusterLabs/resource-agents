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
	cd convert && ${MAKE} all
	cd debug && ${MAKE} all
	cd edit && ${MAKE} all
	cd fsck && ${MAKE} all
	cd libgfs2 && ${MAKE} all
	cd mkfs && ${MAKE} all
	cd mount && ${MAKE} all
	cd quota && ${MAKE} all
	cd tool && ${MAKE} all

copytobin:
	cd convert && ${MAKE} copytobin
	cd debug && ${MAKE} copytobin
	cd edit && ${MAKE} copytobin
	cd fsck && ${MAKE} copytobin
	cd libgfs2 && ${MAKE} copytobin
	cd mkfs && ${MAKE} copytobin
	cd mount && ${MAKE} copytobin
	cd quota && ${MAKE} copytobin
	cd tool && ${MAKE} copytobin

clean:
	cd convert && ${MAKE} clean
	cd bin && ${MAKE} clean
	cd debug && ${MAKE} clean
	cd fsck && ${MAKE} clean
	cd edit && ${MAKE} clean
	cd libgfs2 && ${MAKE} clean
	cd mkfs && ${MAKE} clean
	cd mount && ${MAKE} clean
	cd quota && ${MAKE} clean
	cd tool && ${MAKE} clean

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

