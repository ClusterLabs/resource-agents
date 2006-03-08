###############################################################################
###############################################################################
##
##  Copyright (C) 2006 Red Hat, Inc.  All rights reserved.
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
	csnap-agent \
	csnap-create \
	csnap-delete \
	csnap-server \
	mksnapstore

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

copytobin:
