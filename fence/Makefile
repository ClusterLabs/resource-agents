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
all: copytobin

copytobin:
	cd agents && ${MAKE} copytobin
	cd fence_node && ${MAKE} copytobin
#	cd fence_tool && ${MAKE} copytobin
	cd fenced && ${MAKE} copytobin

clean:
	cd agents && ${MAKE} clean
	cd bin && ${MAKE} clean
	cd fence_node && ${MAKE} clean
#	cd fence_tool && ${MAKE} clean
	cd fenced && ${MAKE} clean

distclean: clean
	rm -f make/defines.mk

install:
	cd bin && ${MAKE} install
	cd man && ${MAKE} install
	cd init.d && ${MAKE} install

uninstall:
	cd bin && ${MAKE} uninstall
	cd man && ${MAKE} uninstall
	cd init.d && ${MAKE} uninstall
