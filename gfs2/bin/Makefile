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

top_srcdir = ..
UNINSTALL = ${top_srcdir}/scripts/uninstall.pl

SBINPROGS= \
	gfs2_mkfs \
	gfs2_quota \
	gfs2_tool

include ${top_srcdir}/make/defines.mk

all:
	cd ..; ${MAKE} copytobin

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
	# Make the hardlink to mkfs_gfs2 instead of copying a separate version
	(cd ${sbindir}; ln -f gfs2_mkfs mkfs.gfs2)

uninstall:
	#uninstall the hardlinked program as well
	${UNINSTALL} ${SBINPROGS} mkfs.gfs2 ${sbindir}


