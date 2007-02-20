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
top_srcdir=..
UNINSTALL = ${top_srcdir}/scripts/uninstall.pl
TARGETS = clubufflush.8 clufindhostname.8 clulog.8 \
	  clurgmgrd.8 clurmtabd.8 clustat.8 clusvcadm.8

include ${top_srcdir}/make/defines.mk

install:
	install -d ${mandir}/man8
	install ${TARGETS} ${mandir}/man8

uninstall:
	${UNINSTALL} ${TARGETS} ${mandir}/man8
