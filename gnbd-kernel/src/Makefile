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

linux_orig = ${top_srcdir}/patches/linux-orig
linux_patched = ${top_srcdir}/patches/linux-patched


PATCH_TARGET = gnbd.patch

PWD := $(shell pwd)
   
obj-m := gnbd.o
lock_gnbd-objs := gnbd.c

EXTRA_CFLAGS += -I$(obj)

all:
	rm -f linux
	ln -s . linux
	${MAKE} -C ${KERNEL_SRC} M=${PWD} modules USING_KBUILD=yes
 
install: all
	install -d ${module_dir}/drivers/block/gnbd
	install gnbd.ko ${module_dir}/drivers/block/gnbd

uninstall:
	${UNINSTALL} gnbd.ko ${module_dir}/drivers/block/gnbd
                                                                                
clean:
	rm -rf linux *.mod.c .gnbd.ko.cmd \
		.tmp_versions *o .*.o.cmd *~


clean:


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
