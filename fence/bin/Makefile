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

UNINSTALL= ${top_srcdir}/scripts/uninstall.pl

SBINPROGS= \
	fence_ack_manual \
	fence_apc \
	fence_brocade \
	fence_egenera \
	fence_ilo \
	fence_manual \
	fence_mcdata \
	fence_node \
	fence_sanbox2 \
	fence_tool \
	fence_vixel \
	fence_wti \
	fenced


include ${top_srcdir}/make/defines.mk

all:
	cd ..; ${MAKE} all

clean:
	@echo -n Removing all copied files...
	@for f in `ls . | grep -v -i "makefile\|cvs"`; do rm -f $$f; done
	@echo done.

install: all
	if [ ! -d ${sbindir} ]; then \
		install -d ${sbindir}; \
	fi
	for v in ${SBINPROGS}; do \
		install -m755 $${v} ${sbindir}; \
	done

uninstall:
	${UNINSTALL} ${SBINPROGS} ${sbindir}

