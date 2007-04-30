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

include ../../make/defines.mk

install:
	install -d ${mandir}/man5
	install -d ${mandir}/man7
	install -d ${mandir}/man8
	install cluster.conf.5 ${mandir}/man5
	install ccs.7 ${mandir}/man7
	install ccsd.8 ccs_test.8 ccs_tool.8 ${mandir}/man8

uninstall:
	${UNINSTALL} cluster.conf.5 ${mandir}/man5
	${UNINSTALL} ccs.7 ${mandir}/man7
	${UNINSTALL} ccsd.8 ccs_test.8 ccs_tool.8 ${mandir}/man8

all:

clean:
