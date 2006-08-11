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
	${MAKE} -C src all

clean:
	${MAKE} -C src clean

distclean: clean
	rm -f make/defines.mk

install: all
	${MAKE} -C src install
	${MAKE} -C man install
	${MAKE} -C init.d install

check:
	${MAKE} -C src check

uninstall:
	${MAKE} -C src uninstall
