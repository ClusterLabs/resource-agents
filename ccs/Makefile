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
	cd daemon && ${MAKE} all
	cd lib && ${MAKE} all
	cd ccs_test && ${MAKE} all
	cd ccs_tool && ${MAKE} all
	cd cluster_conf && ${MAKE} all

copytobin:
	cd daemon && ${MAKE} copytobin
	cd lib && ${MAKE} copytobin
	cd ccs_test && ${MAKE} copytobin
	cd ccs_tool && ${MAKE} copytobin
	cd cluster_conf && ${MAKE} copytobin

clean:
	cd bin && ${MAKE} clean
	cd daemon && ${MAKE} clean
	cd lib && ${MAKE} clean
	cd ccs_test && ${MAKE} clean
	cd ccs_tool && ${MAKE} clean
	cd cluster_conf && ${MAKE} clean
	rm -f *~

distclean: clean
	rm -f make/defines.mk

install:
	cd daemon && ${MAKE} install 
	cd lib && ${MAKE} install
	cd ccs_test && ${MAKE} install
	cd ccs_tool && ${MAKE} install
	cd cluster_conf && ${MAKE} install
	cd man && ${MAKE} install
	cd init.d && ${MAKE} install

uninstall:
	cd daemon && ${MAKE} uninstall 
	cd lib && ${MAKE} uninstall
	cd ccs_test && ${MAKE} uninstall
	cd ccs_tool && ${MAKE} uninstall
	cd man && ${MAKE} uninstall
	cd init.d && ${MAKE} uninstall
