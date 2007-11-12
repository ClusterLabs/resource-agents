###############################################################################
###############################################################################
##
##  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
##  Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

include ../../make/defines.mk

TARGET= libgfs2.a

CFLAGS += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -D_GNU_SOURCE
CFLAGS += -I${KERNEL_SRC}/fs/gfs2/ -I${KERNEL_SRC}/include/
CFLAGS += -I../include
CFLAGS += -I${incdir}

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

all: ${TARGET}

${TARGET}: ${OBJS}
	${AR} cru $@ $^
	${RANLIB} $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: all

uninstall:

clean:
	rm -f *.o ${TARGET}

.PHONY: all
