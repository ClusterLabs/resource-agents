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

PWD := $(shell pwd)

obj-m := dm-log_cluster.o
 
EXTRA_CFLAGS += -I$(obj) -Idrivers/md

all:
	if [ ! -e cluster ]; then ln -s . cluster; fi
	if [ ! -e service.h ]; then cp ${incdir}/cluster/service.h .; fi
	if [ ! -e cnxman.h ]; then cp ${incdir}/cluster/cnxman.h .; fi
	if [ ! -e cnxman-socket.h ]; then cp ${incdir}/cluster/cnxman-socket.h .; fi
	${MAKE} -C ${KERNEL_SRC} M=${PWD} modules USING_KBUILD=yes

install: all
	install -d ${module_dir}/cluster
	install dm-log_cluster.ko ${module_dir}/cluster

uninstall:
	${UNINSTALL} dm-log_cluster.ko ${module_dir}/cluster

clean:
	rm -rf *.ko *.o *.mod.c *~ .*o.cmd .tmp_versions

