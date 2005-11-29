###############################################################################
###############################################################################
##
##  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
##  
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

top_srcdir=.
UNINSTALL=${top_srcdir}/scripts/uninstall.pl

include ${top_srcdir}/make/defines.mk

all:
	cd src && ${MAKE} all

clean:
	cd src && ${MAKE} clean

distclean: clean
	rm -f make/defines.mk

install:
	cd src && ${MAKE} install
	cd man && ${MAKE} install
	cd init.d && ${MAKE} install
	install -d ${DESTDIR}/usr/share/snmp/mibs
	install REDHAT-CLUSTER-MIB ${DESTDIR}/usr/share/snmp/mibs/

uninstall:
	cd src && ${MAKE} uninstall
	cd man && ${MAKE} uninstall
	cd init.d && ${MAKE} uninstall
	${UNINSTALL} REDHAT-CLUSTER-MIB ${DESTDIR}/usr/share/snmp/mibs/

check:
	cd src && ${MAKE} check

rebuild: clean all
