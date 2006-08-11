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
	${MAKE} -C src all

clean:
	${MAKE} -C src clean

distclean: clean
	rm -f make/defines.mk

install: all
	${MAKE} -C src install
	${MAKE} -C man install
	${MAKE} -C init.d install
	install -d ${DESTDIR}/usr/share/snmp/mibs
	install REDHAT-MIB ${DESTDIR}/usr/share/snmp/mibs/
	install REDHAT-CLUSTER-MIB ${DESTDIR}/usr/share/snmp/mibs/

uninstall:
	${MAKE} -C src uninstall
	${MAKE} -C man uninstall
	${MAKE} -C init.d uninstall
	${UNINSTALL} REDHAT-MIB ${DESTDIR}/usr/share/snmp/mibs/
	${UNINSTALL} REDHAT-CLUSTER-MIB ${DESTDIR}/usr/share/snmp/mibs/

check:
	${MAKE} -C src check

rebuild: clean all
