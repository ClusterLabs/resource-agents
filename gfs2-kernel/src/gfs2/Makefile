###############################################################################
###############################################################################
##
##  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
##  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

top_srcdir = ../..
ifndef USING_KBUILD
include ${top_srcdir}/make/defines.mk
UNINSTALL=${top_srcdir}/scripts/uninstall.pl
endif

linux_orig = ${top_srcdir}/src/a
linux_patched = ${top_srcdir}/src/b

TARGET = gfs2.patch

PWD := $(shell pwd)

obj-m := gfs2.o
gfs2-objs := \
	acl.o \
	bits.o \
	bmap.o \
	daemon.o \
	debug.o \
	diaper.o \
	dir.o \
	eaops.o \
	eattr.o \
	glock.o \
	glops.o \
	inode.o \
	ioctl.o \
	jdata.o \
	lm.o \
	log.o \
	lops.o \
	lvb.o \
	main.o \
	meta_io.o \
	mount.o \
	ondisk.o \
	ops_address.o \
	ops_dentry.o \
	ops_export.o \
	ops_file.o \
	ops_fstype.o \
	ops_inode.o \
	ops_super.o \
	ops_vm.o \
	page.o \
	proc.o \
	quota.o \
	resize.o \
	recovery.o \
	rgrp.o \
	super.o \
	trans.o \
	unlinked.o \
	util.o

EXTRA_CFLAGS += -I$(obj)

#EXTRA_CFLAGS += -DGFS2_MEMORY_SIMPLE
#EXTRA_CFLAGS += -DGFS2_MEMORY_BRUTE

all:
	rm -f linux lm_interface.h
	ln -s . linux
	ln -s ${top_srcdir}/../gfs-kernel/src/harness/lm_interface.h .
	${MAKE} -C ${KERNEL_SRC} M=${PWD} modules USING_KBUILD=yes

install: all
	install -d ${module_dir}/fs/gfs2
	install gfs2.ko ${module_dir}/fs/gfs2
	install -d ${incdir}/linux
	install gfs2_ondisk.h gfs2_ioctl.h ${incdir}/linux

uninstall:
	${UNINSTALL} gfs2.ko ${module_dir}/fs/gfs2
	${UNINSTALL} gfs2_ondisk.h gfs2_ioctl.h ${incdir}/linux

clean:
	rm -rf linux *.o .*.o.cmd .gfs2.ko.cmd lm_interface.h \
		gfs2.ko gfs2.mod.c .tmp_versions *~ gfs2_debug_const.h


patches: pre add post

pre:
	@if [ ! -d ${linux_orig} ] ; then \
		echo "No linux source directory (${linux_orig})" ; \
		exit 1; \
	fi
	mkdir -p ${linux_patched}
	rsync -a --delete ${linux_orig}/ ${linux_patched}/

post:
	( cd ${top_srcdir}/src ; diff -urpN a b > ${TARGET} ; cat ../patches/gfs2-build.patch >> ${TARGET} ; exit 0 )

add:
	mkdir -p ${linux_patched}/fs/gfs2
	cp *.[ch] ${linux_patched}/fs/gfs2
	cp Makefile.kernel ${linux_patched}/fs/gfs2/Makefile
	mv ${linux_patched}/fs/gfs2/gfs2_*.h ${linux_patched}/include/linux

