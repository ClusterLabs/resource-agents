###############################################################################
###############################################################################
##
##  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

top_srcdir=..
UNINSTALL = ${top_srcdir}/scripts/uninstall.pl

include ${top_srcdir}/make/defines.mk

all:
	cd .. && ${MAKE} copytobin

clean:
	rm -f *~ *.o ccs_test ccsd

install:
	cd .. && ${MAKE} install

uninstall:
	cd .. && ${MAKE} uninstall
