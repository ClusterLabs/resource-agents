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
ifndef USING_KBUILD
include ${top_srcdir}/make/defines.mk
UNINSTALL=${top_srcdir}/scripts/uninstall.pl
endif

linux_orig = ${top_srcdir}/linux-orig
linux_patched = ${top_srcdir}/linux-patched

TARGET = dlm.patch

PWD := $(shell pwd)

obj-m := dlm.o
dlm-objs := member.o \
            member_ioctl.o \
            member_sysfs.o \
            debug_fs.o \
            ast.o \
            config.o \
            device.o \
            lkb.o \
            locking.o \
            lockqueue.o \
            lockspace.o \
            lowcomms.o \
            main.o \
            memory.o \
            midcomms.o \
            queries.o \
            rebuild.o \
            reccomms.o \
            recover.o \
            recoverd.o \
            rsb.o \
            util.o \
            dir.o

EXTRA_CFLAGS += -I$(obj)

all:
	if [ ! -e cluster ]; then ln -s . cluster; fi
	${MAKE} -C ${KERNEL_SRC} M=${PWD} modules USING_KBUILD=yes

install: all
	install -d ${module_dir}/cluster
	install dlm.ko ${module_dir}/cluster
	install -d ${incdir}/cluster
	install dlm.h dlm_device.h ${incdir}/cluster

uninstall:
	${UNINSTALL} dlm.ko ${module_dir}/cluster
	${UNINSTALL} dlm.h dlm_device.h ${incdir}/cluster




patches: pre add post

pre:
	@if [ ! -d ${linux_orig} ] ; then \
		echo "No linux source directory (${linux_orig})" ; \
		exit 1; \
	fi
	mkdir -p ${linux_patched}
	rsync -a --delete ${linux_orig}/ ${linux_patched}/

post:
	( cd ${top_srcdir} ; diff -urN linux-orig linux-patched > ${TARGET} ; exit 0 )

add:
	mkdir -p ${linux_patched}/cluster/dlm
	mkdir -p ${linux_patched}/include/cluster
	cp *.[ch] ${linux_patched}/cluster/dlm
	mv ${linux_patched}/cluster/dlm/dlm.h ${linux_patched}/include/cluster
	mv ${linux_patched}/cluster/dlm/dlm_device.h ${linux_patched}/include/cluster

clean:
	rm -rf cluster *.ko *.o *.mod.c *~ .*o.cmd .tmp_versions
