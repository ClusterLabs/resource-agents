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
	cd lib && ${MAKE} all
	cd tests && ${MAKE} all

clean:
	rm -f *~
	cd lib && ${MAKE} clean
	cd tests && ${MAKE} clean

install:
	cd lib && ${MAKE} install
	cd man && ${MAKE} install
	cd tests && ${MAKE} install

uninstall:
	cd lib && ${MAKE} uninstall
	cd man && ${MAKE} uninstall
	cd tests && ${MAKE} uninstall

distclean: clean
	rm -f make/defines.mk
