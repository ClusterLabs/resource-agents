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

all:

clean:

distclean:

install:
	install -d ${mandir}/man5
	install -d ${mandir}/man8
	install cman.5 qdisk.5 ${mandir}/man5
	install cman_tool.8 qdiskd.8 mkqdisk.8 ${mandir}/man8

uninstall:
	${UNINSTALL} cman.5 qdisk.5 ${mandir}/man5
	${UNINSTALL} cman_tool.8 qdiskd.8 mkqdisk.8 ${mandir}/man8

