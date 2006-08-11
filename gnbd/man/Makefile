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

TARGET8= \
	gnbd.8 \
	gnbd_export.8 \
	gnbd_import.8 \
	gnbd_serv.8 \
	fence_gnbd.8

UNINSTALL=${top_srcdir}/scripts/uninstall.pl

top_srcdir=..

include ${top_srcdir}/make/defines.mk

all:

clean:

install: all
	install -d ${mandir}/man8
	install ${TARGET8} ${mandir}/man8

uninstall:
	${UNINSTALL} ${TARGET8} ${mandir}/man8
