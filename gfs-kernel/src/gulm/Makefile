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

top_srcdir = ../..
ifndef USING_KBUILD
include ${top_srcdir}/make/defines.mk
UNINSTALL=${top_srcdir}/scripts/uninstall.pl
endif

linux_orig = ${top_srcdir}/src/linux-orig
linux_patched = ${top_srcdir}/src/linux-patched


TARGET = lock_gulm.patch

PWD := $(shell pwd)
  
obj-m := lock_gulm.o
lock_gulm-objs := gulm_core.o \
	gulm_firstlock.o \
	gulm_fs.o \
	gulm_jid.o \
	gulm_lock_queue.o \
	gulm_lt.o \
	gulm_main.o \
	gulm_plock.o \
	handler.o \
	lg_core.o \
	lg_lock.o \
	lg_main.o \
	utils_tostr.o \
	xdr_base.o \
	xdr_io.o \
	xdr_socket.o

EXTRA_CFLAGS += -I$(obj)

all:
	rm -f linux lm_interface.h
	ln -s . linux
	ln -s ${top_srcdir}/src/harness/lm_interface.h .
	${MAKE} -C ${KERNEL_SRC} M=${PWD} modules USING_KBUILD=yes

install: all
	install -d ${module_dir}/fs/gfs_locking/lock_gulm
	install lock_gulm.ko ${module_dir}/fs/gfs_locking/lock_gulm
 
uninstall:
	${UNINSTALL} lock_gulm.ko ${module_dir}/fs/gfs_locking/lock_gulm

clean:
	rm -rf linux lm_interface.h *.mod.c .lock_gulm.ko.cmd \
		.tmp_versions *o .*.o.cmd *~

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
	mkdir -p ${linux_patched}/fs/gfs_locking/lock_gulm
	cp *.[ch] ${linux_patched}/fs/gfs_locking/lock_gulm
