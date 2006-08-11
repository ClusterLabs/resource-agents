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
	${MAKE} -C lib all

install: all
	${MAKE} -C lib install

uninstall:
	${MAKE} -C lib uninstall

clean:
	${MAKE} -C lib clean
	rm -f *~

distclean: clean
	rm -f make/defines.mk
