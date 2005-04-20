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

obj-m := dlm.o dlm_device.o
dlm-objs :=	ast.o \
		config.o \
		debug_fs.o \
		dir.o \
		lock.o \
		lockspace.o \
		lowcomms.o \
		main.o \
		member.o \
		member_sysfs.o \
		memory.o \
		midcomms.o \
		node_ioctl.o \
		rcom.o \
		recover.o \
		recoverd.o \
		requestqueue.o \
		util.o 

dlm_device-objs := device.o 

EXTRA_CFLAGS += -I$(obj) -DCONFIG_DLM_DEBUG

all:
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
	mkdir -p ${linux_patched}/drivers/dlm
	cp *.[ch] ${linux_patched}/drivers/dlm
	cp Makefile.kernel ${linux_patched}/drivers/dlm/Makefile
	cp Kconfig ${linux_patched}/drivers/dlm/
	mv ${linux_patched}/drivers/dlm/dlm.h ${linux_patched}/include/linux
	mv ${linux_patched}/drivers/dlm/dlm_device.h ${linux_patched}/include/linux
	mv ${linux_patched}/drivers/dlm/dlm_node.h ${linux_patched}/include/linux

clean:
	rm -rf cluster *.ko *.o *.mod.c *~ .*o.cmd .tmp_versions
