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
	cd tool && ${MAKE} all
	cd dlm_controld && ${MAKE} all
	cd gfs_controld && ${MAKE} all

clean:
	cd lib && ${MAKE} clean
	cd daemon && ${MAKE} clean
	cd tool && ${MAKE} clean
	cd dlm_controld && ${MAKE} clean
	cd gfs_controld && ${MAKE} clean

install:
	cd lib && ${MAKE} install
	cd daemon && ${MAKE} install
	cd tool && ${MAKE} install
	cd dlm_controld && ${MAKE} install
	cd gfs_controld && ${MAKE} install

distclean: clean
	rm -f make/defines.mk

