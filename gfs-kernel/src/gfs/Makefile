TARGET= gfs.ko

KMODT=$(TARGET)
KDIRT=fs/gfs
KHEADT=gfs_ondisk.h gfs_ioctl.h

all: ${TARGET}

ifndef USING_KBUILD
include ../../../make/defines.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk
endif

PWD := $(shell pwd)

SYMVERFILE := ${KERNEL_BUILD}/Module.symvers

obj-m := gfs.o
gfs-objs := acl.o \
	bits.o \
	bmap.o \
	daemon.o \
	dio.o \
	dir.o \
	eaops.o \
	eattr.o \
	file.o \
	glock.o \
	glops.o \
	inode.o \
	ioctl.o \
	lm.o \
	log.o \
	lops.o \
	lvb.o \
	main.o \
	mount.o \
	ondisk.o \
	ops_address.o \
	ops_dentry.o \
	ops_export.o \
	ops_file.o \
	ops_fstype.o \
	ops_inode.o \
	ops_super.o \
	ops_vm.o \
	page.o \
	proc.o \
	quota.o \
	recovery.o \
	rgrp.o \
	super.o \
	sys.o \
	trans.o \
	unlinked.o \
	util.o

${TARGET}:
	${MAKE} -C ${KERNEL_BUILD} M=${PWD} symverfile=${SYMVERFILE} modules USING_KBUILD=yes

clean: generalclean
