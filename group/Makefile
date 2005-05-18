###############################################################################
###############################################################################
##
##  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

all:
	cd lib && ${MAKE} all
	cd daemon && ${MAKE} all

clean:
	cd lib && ${MAKE} clean
	cd daemon && ${MAKE} clean

distclean: clean
	rm -f make/defines.mk

install:
	cd lib && ${MAKE} install
	cd daemon && ${MAKE} clean

