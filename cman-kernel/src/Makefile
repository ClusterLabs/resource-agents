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

TARGET = cman.patch

PWD := $(shell pwd)

obj-m := cman.o
cman-objs := cnxman.o config.o membership.o proc.o \
	sm_barrier.o sm_control.o sm_daemon.o sm_joinleave.o\
	sm_membership.o sm_message.o sm_misc.o sm_recover.o sm_services.o \
	sm_user.o
 
EXTRA_CFLAGS += -I$(obj)

all:
	rm -f cluster
	ln -s . cluster
	${MAKE} -C ${KERNEL_SRC} M=${PWD} modules USING_KBUILD=yes

install: all
	install -d ${module_dir}/cluster
	install cman.ko ${module_dir}/cluster
	install -d ${incdir}/cluster
	install cnxman.h cnxman-socket.h service.h ${incdir}/cluster

uninstall:
	${UNINSTALL} cman.ko ${module_dir}/cluster
	${UNINSTALL} cnxman.h cnxman-socket.h service.h ${incdir}/cluster



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
	mkdir -p ${linux_patched}/cluster/cman
	mkdir -p ${linux_patched}/include/cluster
	cp *.[ch] ${linux_patched}/cluster/cman
	mv ${linux_patched}/cluster/cman/cnxman.h ${linux_patched}/include/cluster
	mv ${linux_patched}/cluster/cman/cnxman-socket.h ${linux_patched}/include/cluster
	mv ${linux_patched}/cluster/cman/service.h ${linux_patched}/include/cluster

clean:
	rm -rf cluster *.ko *.o *.mod.c *~ .*o.cmd .tmp_versions

