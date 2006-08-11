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
	${MAKE} -C client all
	${MAKE} -C server all
	${MAKE} -C tools all

clean:
	${MAKE} -C client clean
	${MAKE} -C server clean
	${MAKE} -C tools clean

distclean: clean
	rm -f make/defines.mk

install: all
	${MAKE} -C client install
	${MAKE} -C server install
	${MAKE} -C tools install
	${MAKE} -C man install

deinstall uninstall:
	${MAKE} -C client uninstall
	${MAKE} -C server uninstall
	${MAKE} -C tools uninstall
	${MAKE} -C man uninstall
