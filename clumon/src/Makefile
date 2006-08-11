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


all:
	${MAKE} -C common all
	${MAKE} -C daemon all
	${MAKE} -C snmp-agent all
	${MAKE} -C cim-provider all

clean:
	${MAKE} -C common clean
	${MAKE} -C daemon clean
	${MAKE} -C snmp-agent clean
	${MAKE} -C cim-provider clean

check:
	${MAKE} -C common check
	${MAKE} -C daemon check
	${MAKE} -C snmp-agent check
	${MAKE} -C cim-provider check

distclean: clean

install: all
	${MAKE} -C common install
	${MAKE} -C daemon install
	${MAKE} -C snmp-agent install
	${MAKE} -C cim-provider install

uninstall:
	${MAKE} -C common uninstall
	${MAKE} -C daemon uninstall
	${MAKE} -C snmp-agent uninstall
	${MAKE} -C cim-provider uninstall

rebuild: clean all
