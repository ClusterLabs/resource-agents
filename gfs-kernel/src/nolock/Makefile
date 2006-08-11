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

linux_orig = ${top_srcdir}/src/a
linux_patched = ${top_srcdir}/src/b


TARGET = lock_nolock.patch

PWD := $(shell pwd)

obj-m := lock_nolock.o
lock_nolock-objs := main.o

EXTRA_CFLAGS += -I$(obj)

all:
	${MAKE} -C ${KERNEL_SRC} M=${PWD} modules USING_KBUILD=yes

install: all
	install -d ${module_dir}/fs/gfs_locking/lock_nolock
	install lock_nolock.ko ${module_dir}/fs/gfs_locking/lock_nolock

uninstall:
	${UNINSTALL} lock_nolock.ko ${module_dir}/fs/gfs_locking/lock_nolock

clean:
	rm -rf linux *.mod.c .lock_nolock.ko.cmd \
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
	( cd ${top_srcdir}/src ; diff -urpN a b > ${TARGET} ; exit 0 )

add:
	mkdir -p ${linux_patched}/fs/gfs2/locking/nolock
	cp *.[ch] ${linux_patched}/fs/gfs2/locking/nolock
	cp Makefile.kernel ${linux_patched}/fs/gfs2/locking/nolock/Makefile

