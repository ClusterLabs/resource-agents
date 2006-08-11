###############################################################################
###############################################################################
##
##  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
##  Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

top_srcdir = $(shell pwd)/../..
ifndef USING_KBUILD
include ${top_srcdir}/make/defines.mk
UNINSTALL=${top_srcdir}/scripts/uninstall.pl
endif

linux_orig = ${top_srcdir}/src/linux-orig
linux_patched = ${top_srcdir}/src/linux-patched

PWD := $(shell pwd)

TARGET = gfs.patch
SYMVERFILE := ${KERNEL_SRC}/Module.symvers

obj-m := gfs.o
gfs-objs := acl.o \
	bits.o \
	bmap.o \
	daemon.o \
	dio.o \
	dir.o \
	eaops.o \
	eattr.o \
	file.o \
	glock.o \
	glops.o \
	inode.o \
	ioctl.o \
	lm.o \
	log.o \
	lops.o \
	lvb.o \
	main.o \
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
	recovery.o \
	rgrp.o \
	super.o \
	sys.o \
	trans.o \
	unlinked.o \
	util.o

EXTRA_CFLAGS += -I$(obj) -I${incdir} -I/usr/include 

all:
	${MAKE} -C ${KERNEL_SRC} M=${PWD} symverfile=${SYMVERFILE} modules USING_KBUILD=yes

install: all
	install -d ${module_dir}/fs/gfs
	install gfs.ko ${module_dir}/fs/gfs
	install -d ${incdir}/linux
	install gfs_ondisk.h gfs_ioctl.h ${incdir}/linux

uninstall:
	${UNINSTALL} gfs.ko ${module_dir}/fs/gfs
	${UNINSTALL} gfs_ondisk.h gfs_ioctl.h ${incdir}/linux

clean:
	rm -rf linux *.o .*.o.cmd .gfs.ko.cmd \
		gfs.ko gfs.mod.c .tmp_versions *~ *~* Modules.symvers

distclean: clean

patches: pre add post

pre:
	@if [ ! -d ${linux_orig} ] ; then \
		echo "No linux source directory (${linux_orig})" ; \
		exit 1; \
	fi
	mkdir -p ${linux_patched}
	rsync -a --delete ${linux_orig}/ ${linux_patched}/

post:
	( cd ${top_srcdir}/src ; diff -urN linux-orig linux-patched > ${TARGET} ; exit 0 )

add:
	mkdir -p ${linux_patched}/fs/gfs
	cp *.[ch] ${linux_patched}/fs/gfs
	mv ${linux_patched}/fs/gfs/gfs_*.h ${linux_patched}/include/linux

