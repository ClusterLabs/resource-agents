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
	gfs.8 \
	gfs_fsck.8 \
	gfs_grow.8 \
	gfs_jadd.8 \
	gfs_mkfs.8 \
	gfs_quota.8 \
	gfs_tool.8


top_srcdir = ..

UNINSTALL = ${top_srcdir}/scripts/uninstall.pl

include ${top_srcdir}/make/defines.mk

install:
	install -d ${mandir}/man8
	install ${TARGET8} ${mandir}/man8

uninstall:
	${UNINSTALL} ${TARGET8} ${mandir}/man8
