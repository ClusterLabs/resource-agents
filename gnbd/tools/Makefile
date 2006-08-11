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
	${MAKE} -C fence_gnbd all
	${MAKE} -C gnbd_export all
	${MAKE} -C gnbd_import all

install: all
	${MAKE} -C fence_gnbd install
	${MAKE} -C gnbd_export install
	${MAKE} -C gnbd_import install

clean:
	${MAKE} -C fence_gnbd clean
	${MAKE} -C gnbd_export clean
	${MAKE} -C gnbd_import clean
