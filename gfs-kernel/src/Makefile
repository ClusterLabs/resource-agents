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
	cd harness && ${MAKE} all
	cd nolock && ${MAKE} all
	cd dlm && ${MAKE} all
	cd gulm && ${MAKE} all
	cd gfs && ${MAKE} all

install:
	cd harness && ${MAKE} install
	cd nolock && ${MAKE} install
	cd dlm && ${MAKE} install
	cd gulm && ${MAKE} install
	cd gfs && ${MAKE} install

uninstall:
	cd harness && ${MAKE} uninstall
	cd nolock && ${MAKE} uninstall
	cd dlm && ${MAKE} uninstall
	cd gulm && ${MAKE} uninstall
	cd gfs && ${MAKE} uninstall

clean:
	rm -rf *.patch linux-patched
	cd harness && ${MAKE} clean
	cd nolock && ${MAKE} clean
	cd dlm && ${MAKE} clean
	cd gulm && ${MAKE} clean
	cd gfs && ${MAKE} clean

patches:
	cd gfs && ${MAKE} patches
	cd harness && ${MAKE} patches
	cd nolock && ${MAKE} patches
	cd dlm && ${MAKE} patches
	cd gulm && ${MAKE} patches


