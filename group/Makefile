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
	${MAKE} -C lib all
	${MAKE} -C daemon all
	${MAKE} -C tool all
	${MAKE} -C dlm_controld all
	${MAKE} -C gfs_controld all

clean:
	${MAKE} -C lib clean
	${MAKE} -C daemon clean
	${MAKE} -C tool clean
	${MAKE} -C dlm_controld clean
	${MAKE} -C gfs_controld clean

install: all
	${MAKE} -C lib install
	${MAKE} -C daemon install
	${MAKE} -C tool install
	${MAKE} -C dlm_controld install
	${MAKE} -C gfs_controld install

distclean: clean
	rm -f make/defines.mk
