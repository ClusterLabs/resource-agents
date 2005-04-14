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

top_srcdir = ../../..
ifndef USING_KBUILD
include ${top_srcdir}/make/defines.mk
UNINSTALL=${top_srcdir}/scripts/uninstall.pl
endif

linux_orig = ${top_srcdir}/src/linux-orig
linux_patched = ${top_srcdir}/src/linux-patched


TARGET = lock_dlm.patch

PWD := $(shell pwd)
 
obj-m := lock_dlm.o
lock_dlm-objs := lock.o \
	main.o \
	mount.o \
	thread.o \
	sysfs.o

EXTRA_CFLAGS += -I$(obj)

all:
	if [ ! -e linux ]; then ln -s . linux; fi
	if [ ! -e lm_interface.h ]; then ln -s ${top_srcdir}/src/harness/lm_interface.h .; fi
	if [ ! -e dlm.h ]; then cp ${incdir}/cluster/dlm.h .; fi
	${MAKE} -C ${KERNEL_SRC} M=${PWD} modules USING_KBUILD=yes

install: all
	install -d ${module_dir}/fs/gfs_locking/lock_dlm
	install lock_dlm.ko ${module_dir}/fs/gfs_locking/lock_dlm
 
uninstall:
	${UNINSTALL} lock_dlm.ko ${module_dir}/fs/gfs_locking/lock_dlm
 
clean:
	rm -rf linux cluster lm_interface.h dlm.h cnxman.h \
		cnxman-socket.h service.h .tmp_versions *.o \
		.*.o.cmd lock_dlm.ko .*.ko.cmd lock_dlm.mod.c *~

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
	mkdir -p ${linux_patched}/fs/gfs_locking/lock_dlm
	cp *.[ch] ${linux_patched}/fs/gfs_locking/lock_dlm
