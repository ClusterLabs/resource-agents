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
top_srcdir=..
include ${top_srcdir}/make/defines.mk
UNINSTALL = ${top_srcdir}/scripts/uninstall.pl

install:
	install -d ${mandir}/man3
	install -d ${mandir}/man8
	install clu_connect.3 clu_disconnect.3 clu_get_event.3 ${mandir}/man3
	install magma_tool.8 ${mandir}/man8

uninstall:
	${UNINSTALL} clu_connect.3 clu_disconnect.3 clu_get_event.3 ${mandir}/man3
	${UNINSTALL} magma_tool.8 ${mandir}/man8
