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
	cd resources && ${MAKE} all
	cd clulib && ${MAKE} all
	cd daemons && ${MAKE} all
	cd utils && ${MAKE} all

clean:
	cd resources && ${MAKE} clean
	cd clulib && ${MAKE} clean
	cd daemons && ${MAKE} clean
	cd utils && ${MAKE} clean

check:
	cd resources && ${MAKE} check
	cd daemons && ${MAKE} check

distclean: clean
	rm -f make/defines.mk

install:
	cd resources && ${MAKE} install
	cd clulib && ${MAKE} install
	cd daemons && ${MAKE} install
	cd utils && ${MAKE} install

uninstall:
	cd resources && ${MAKE} uninstall
	cd clulib && ${MAKE} uninstall
	cd daemons && ${MAKE} uninstall
	cd utils && ${MAKE} uninstall
