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
	cd cman_tool && ${MAKE} all

copytobin:
	cd cman_tool && ${MAKE} copytobin

clean:
	cd bin && ${MAKE} clean
	cd cman_tool && ${MAKE} clean

distclean: clean
	rm -f make/defines.mk

install:
	cd cman_tool && ${MAKE} install
	cd man && ${MAKE} install
	cd init.d && ${MAKE} install

uninstall:
	cd cman_tool && ${MAKE} uninstall
	cd man && ${MAKE} uninstall
	cd init.d && ${MAKE} uninstall
