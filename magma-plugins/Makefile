###############################################################################
###############################################################################
##
##  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################
all:
	cd cman && ${MAKE} all
	cd dumb && ${MAKE} all
	cd gulm && ${MAKE} all
	cd sm && ${MAKE} all

clean:
	cd cman && ${MAKE} clean
	cd dumb && ${MAKE} clean
	cd gulm && ${MAKE} clean
	cd sm && ${MAKE} clean

install:
	cd cman && ${MAKE} install
	cd dumb && ${MAKE} install
	cd gulm && ${MAKE} install
	cd sm && ${MAKE} install

uninstall:
	cd cman && ${MAKE} uninstall
	cd dumb && ${MAKE} uninstall
	cd gulm && ${MAKE} uninstall
	cd sm && ${MAKE} uninstall
	rm -rf /lib/magma

distclean: clean
	rm -f make/defines.mk
