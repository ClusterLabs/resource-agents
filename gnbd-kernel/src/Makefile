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

ifndef USING_KBUILD
include ../../make/defines.mk
include $(OBJDIR)/make/clean.mk
endif

PWD = $(shell pwd)

SYMVERFILE := ${KERNEL_BUILD}/Module.symvers

obj-m := gnbd.o

all:
	${MAKE} -C ${KERNEL_BUILD} M=${PWD} symverfile=${SYMVERFILE} modules USING_KBUILD=yes

install: all
	install -d ${incdir}/linux
	install gnbd.h ${incdir}/linux
	install -d ${module_dir}/drivers/block/gnbd
	install gnbd.ko ${module_dir}/drivers/block/gnbd

uninstall:
	${UNINSTALL} gnbd.ko ${module_dir}/drivers/block/gnbd
	${UNINSTALL} gnbd.h ${incdir}/linux

clean: generalclean

distclean: clean
