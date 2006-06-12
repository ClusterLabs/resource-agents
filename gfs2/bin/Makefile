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
	mkfs.gfs2 \
	mount.gfs2 \
	gfs2_edit \
	gfs2_convert \
	umount.gfs2 \
	gfs2_fsck \
#	gfs2_quota \

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
	(cd ${sbindir}; ln -f mkfs.gfs2 gfs2_grow)

uninstall:
	#uninstall the hardlinked program as well
	${UNINSTALL} ${SBINPROGS} gfs2_convert mkfs.gfs2 gfs2_fsck gfs2_grow gfs2_jadd gfs2_shrink mount.gfs2 umount.gfs2 ${sbindir}


