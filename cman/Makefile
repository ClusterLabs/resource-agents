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
	${MAKE} -C cman_tool all
	${MAKE} -C daemon all
	${MAKE} -C qdisk all

clean:
	${MAKE} -C lib clean
	${MAKE} -C cman_tool clean
	${MAKE} -C daemon clean
	${MAKE} -C qdisk clean

distclean: clean
	rm -f make/defines.mk

install: all
	${MAKE} -C lib install
	${MAKE} -C cman_tool install
	${MAKE} -C daemon install
	${MAKE} -C qdisk install
	${MAKE} -C init.d install
	${MAKE} -C man install

uninstall:
	${MAKE} -C lib uninstall
	${MAKE} -C cman_tool uninstall
	${MAKE} -C daemon uninstall
	${MAKE} -C qdisk uninstall
	${MAKE} -C init.d uninstall
	${MAKE} -C man uninstall
