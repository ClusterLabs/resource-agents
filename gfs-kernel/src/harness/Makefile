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


TARGET = lock_harness.patch

PWD := $(shell pwd)

obj-m := lock_harness.o
lock_harness-objs := main.o

EXTRA_CFLAGS += -I$(obj)

all:
	rm -f linux
	ln -s . linux
	${MAKE} -C ${KERNEL_SRC} M=${PWD} modules USING_KBUILD=yes

install: all
	install -d ${incdir}/linux
	install lm_interface.h ${incdir}/linux
	install -d ${module_dir}/fs/gfs_locking/lock_harness
	install lock_harness.ko ${module_dir}/fs/gfs_locking/lock_harness

uninstall: all
	${UNINSTALL} lm_interface.h ${incdir}/linux
	${UNINSTALL} lock_harness.ko ${module_dir}/fs/gfs_locking/lock_harness
 
clean:
	rm -rf linux *o .*.o.cmd lock_harness.mod.c .lock_harness.ko.cmd .tmp_versions *~
 

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
	mkdir -p ${linux_patched}/fs/gfs_locking/lock_harness
	cp *.[ch] ${linux_patched}/fs/gfs_locking/lock_harness
	mv ${linux_patched}/fs/gfs_locking/lock_harness/lm_interface.h ${linux_patched}/include/linux
