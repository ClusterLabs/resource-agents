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

TARGETS= cman qdiskd

include ../../make/defines.mk

all:

clean:

install:
	install -d ${DESTDIR}/etc/init.d
	install $(S)/cman $(S)/qdiskd ${DESTDIR}/etc/init.d

uninstall:
	${UNINSTALL} ${TARGETS} ${DESTDIR}/etc/init.d
