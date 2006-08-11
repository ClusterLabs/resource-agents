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
	${MAKE} -C resources all
	${MAKE} -C clulib all
	${MAKE} -C daemons all
	${MAKE} -C utils all

clean:
	${MAKE} -C resources clean
	${MAKE} -C clulib clean
	${MAKE} -C daemons clean
	${MAKE} -C utils clean

check:
	${MAKE} -C resources check
	${MAKE} -C daemons check

distclean: clean
	rm -f make/defines.mk

install: all
	${MAKE} -C resources install
	${MAKE} -C clulib install
	${MAKE} -C daemons install
	${MAKE} -C utils install

uninstall:
	${MAKE} -C  resources uninstall
	${MAKE} -C clulib uninstall
	${MAKE} -C daemons uninstall
	${MAKE} -C utils  uninstall
