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

TARGET1= gfs_mkfs
TARGET2= mkfs.gfs

SBINDIRT=$(TARGET1)
SBINSYMT=$(TARGET2)

all: depends ${TARGET1} ${TARGET2}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	device_geometry.o \
	fs_geometry.o \
	locking.o \
	main.o \
	ondisk.o \
	structures.o

CFLAGS += -DHELPER_PROGRAM -D_FILE_OFFSET_BITS=64
CFLAGS += -I${gfskincdir} -I${volidincdir}
CFLAGS += -I$(S)/../include -I$(S)/../libgfs
CFLAGS += -I${incdir}

LDFLAGS += -L${volidlibdir} -lvolume_id
LDFLAGS += -L../libgfs -lgfs


${TARGET1}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET2}: ${TARGET1}
	ln -sf ${TARGET1} ${TARGET2}

depends:
	$(MAKE) -C ../libgfs all

clean: generalclean

.PHONY: all ${TARGET1}
