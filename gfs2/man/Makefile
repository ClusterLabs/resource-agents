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

TARGET8= \
	gfs2.8 \
	gfs2_convert.8 \
	gfs2_edit.8 \
	gfs2_fsck.8 \
	gfs2_grow.8 \
	gfs2_jadd.8 \
	gfs2_mount.8 \
	gfs2_quota.8 \
	gfs2_tool.8 \
	mkfs.gfs2.8

all:

clean:

install:
	install -d ${mandir}/man8
	for i in ${TARGET8}; do \
	 install $(S)/$$i ${mandir}/man8; \
	done

uninstall:
	${UNINSTALL} ${TARGET8} ${mandir}/man8
