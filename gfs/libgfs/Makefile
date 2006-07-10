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

.PHONY: all clean install

top_srcdir=..
include ${top_srcdir}/make/defines.mk

LIBGFS=libgfs.a
INCLUDEPATH=-I${top_srcdir}/include -I${top_srcdir}/config -I${KERNEL_SRC}/fs/gfs2/ -I${KERNEL_SRC}/include/ -I${gfskincdir}

INSTALL=install
CC=gcc -c
LD=gcc
CFLAGS=-Wall -ggdb -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE \
       -D_GNU_SOURCE -DHELPER_PROGRAM -DGFS_RELEASE_NAME=\"2\" ${INCLUDEPATH}

H=gfs_ondisk.h incore.h libgfs.h
C=bio.c bitmap.c block_list.c file.c fs_bits.c fs_bmap.c fs_dir.c \
	fs_inode.c inode.c log.c ondisk.c rgrp.c size.c super.c util.c
O=$(subst .c,.o,${C})

all: ${LIBGFS}

${LIBGFS}: ${O}
	ar cru libgfs.a ${O}
	ranlib libgfs.a

.c.o: $<
	${CC} ${CFLAGS} -o $@ $^

clean:
	rm -f *.o ${LIBGFS}
