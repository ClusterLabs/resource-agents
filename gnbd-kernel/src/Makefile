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
linux_orig = ${top_srcdir}/patches/linux-orig
linux_patched = ${top_srcdir}/patches/linux-patched


PATCH_TARGET = gnbd.patch

patches: pre add post

pre:
	@if [ ! -d ${linux_orig} ] ; then \
		echo "No linux source directory (${linux_orig})" ; \
		exit 1; \
	fi
	mkdir -p ${linux_patched}
	rsync -a --delete ${linux_orig}/ ${linux_patched}/

post:
	( cd ${top_srcdir}/patches ; diff -urN linux-orig linux-patched > ${PATCH_TARGET} ; exit 0 )

add:
	cp gnbd.c ${linux_patched}/drivers/block/
	cp gnbd.h ${linux_patched}/include/linux/

clean:

