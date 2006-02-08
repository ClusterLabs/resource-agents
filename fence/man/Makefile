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
	fence.8 \
	fence_ack_manual.8 \
	fence_apc.8 \
	fence_rsa.8 \
	fence_bladecenter.8 \
	fence_brocade.8 \
	fence_drac.8 \
	fence_egenera.8 \
	fence_ilo.8 \
	fence_manual.8 \
	fence_mcdata.8 \
	fence_node.8 \
        fence_rib.8 \
	fence_sanbox2.8 \
	fence_tool.8 \
	fence_vixel.8 \
	fence_wti.8 \
	fence_bullpap.8 \
	fence_ipmilan.8 \
	fenced.8

UNINSTALL=${top_srcdir}/scripts/uninstall.pl

top_srcdir=..

include ${top_srcdir}/make/defines.mk

install:
	install -d ${mandir}/man8
	install ${TARGET8} ${mandir}/man8

uninstall:
	${UNINSTALL} ${TARGET8} ${mandir}/man8
