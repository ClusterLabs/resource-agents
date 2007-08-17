###############################################################################
###############################################################################
##
##  Copyright (C) 2007 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

include ../../make/defines.mk

TARGET8 = \
	dlm_controld.8 \
	gfs_controld.8 \
	group_tool.8 \
	groupd.8

all:

clean:

install:
	install -d ${mandir}/man8
	install ${TARGET8} ${mandir}/man8

uninstall:
	${UNINSTALL} ${TARGET8} ${mandir}/man8

