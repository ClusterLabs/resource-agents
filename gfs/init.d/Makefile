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

TARGET= gfs

UNINSTALL=${top_srcdir}/scripts/uninstall.pl

top_srcdir=..

include ${top_srcdir}/make/defines.mk

copytobin:

clean:

install:
	install -d ${DESTDIR}/etc/init.d
	install ${TARGET} ${DESTDIR}/etc/init.d

uninstall:
	${UNINSTALL} ${TARGET} ${DESTDIR}/etc/init.d
