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
	cd fence_gnbd && ${MAKE} all
	cd gnbd_export && ${MAKE} all
	cd gnbd_import && ${MAKE} all

copytobin:
	cd fence_gnbd && ${MAKE} copytobin
	cd gnbd_export && ${MAKE} copytobin
	cd gnbd_import && ${MAKE} copytobin

clean:
	cd fence_gnbd && ${MAKE} clean
	cd gnbd_export && ${MAKE} clean
	cd gnbd_import && ${MAKE} clean
