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
	cd lib && ${MAKE} all
	cd daemon && ${MAKE} all

copytobin:
	cd lib && ${MAKE} copytobin
	cd daemon && ${MAKE} copytobin

install:
	cd lib && ${MAKE} install
	cd daemon && ${MAKE} install

uninstall:
	cd lib && ${MAKE} uninstall
	cd daemon && ${MAKE} uninstall

clean:
	cd lib && ${MAKE} clean
	cd daemon && ${MAKE} clean
	rm -f *~

distclean: clean
	rm -f make/defines.mk

