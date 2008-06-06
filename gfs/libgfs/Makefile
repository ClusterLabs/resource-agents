TARGET=libgfs

MAKESTATICLIB = 1

OBJS=	bio.o \
	bitmap.o \
	block_list.o \
	file.o \
	fs_bits.o \
	fs_bmap.o \
	fs_dir.o \
	fs_inode.o \
	inode.o \
	log.o \
	ondisk.o \
	rgrp.o \
	size.o \
	super.o \
	util.o

include ../../make/defines.mk
include $(OBJDIR)/make/libs.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

CFLAGS += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE
CFLAGS += -D_GNU_SOURCE -DHELPER_PROGRAM
CFLAGS += -c
CFLAGS += -I${KERNEL_SRC}/fs/gfs2/ -I${KERNEL_SRC}/include/
CFLAGS += -I${gfskincdir} -I$(S)/../include
CFLAGS += -I${incdir}
