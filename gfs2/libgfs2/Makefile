TARGET= libgfs2

MAKESTATICLIB = 1

OBJS=	bitmap.o \
	block_list.o \
	buf.o \
	device_geometry.o \
	fs_bits.o \
	fs_geometry.o \
	fs_ops.o \
	locking.o \
	gfs2_log.o \
	misc.o \
	ondisk.o \
	recovery.o \
	size.o \
	structures.o \
	super.o \
	rgrp.o

include ../../make/defines.mk
include $(OBJDIR)/make/libs.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

CFLAGS += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -D_GNU_SOURCE
CFLAGS += -I${KERNEL_SRC}/fs/gfs2/ -I${KERNEL_SRC}/include/
CFLAGS += -I$(S)/../include
CFLAGS += -I${incdir}
