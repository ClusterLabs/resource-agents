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

all:
	${MAKE} -C daemon all
	${MAKE} -C lib all
	${MAKE} -C ccs_test all
	${MAKE} -C ccs_tool all

clean:
	${MAKE} -C daemon clean
	${MAKE} -C lib clean
	${MAKE} -C ccs_test clean
	${MAKE} -C ccs_tool clean
	rm -f *~

distclean: clean
	rm -f make/defines.mk

install: all
	${MAKE} -C daemon install 
	${MAKE} -C lib install
	${MAKE} -C ccs_test install
	${MAKE} -C ccs_tool install
	${MAKE} -C man install

uninstall:
	${MAKE} -C daemon uninstall 
	${MAKE} -C lib uninstall
	${MAKE} -Cccs_test  uninstall
	${MAKE} -C ccs_tool uninstall
	${MAKE} -Cman  uninstall
