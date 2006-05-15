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

LIBGFS2=libgfs2.a
INCLUDEPATH=-I${top_srcdir}/include -I${top_srcdir}/config -I${KERNEL_SRC}/fs/gfs2/ -I${KERNEL_SRC}/include/

INSTALL=install
CC=gcc -c
LD=gcc
## Bob: add -O2 back in and take -ggdb out
CFLAGS=-Wall -ggdb -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE \
       -D_GNU_SOURCE -DGFS2_RELEASE_NAME=\"2\" ${INCLUDEPATH}

H=gfs2_disk_hash.h iddev.h libgfs2.h linux_endian.h ondisk.h osi_list.h
C=buf.c device_geometry.c fs_geometry.c fs_ops.c iddev.c locking.c misc.c ondisk.c size.c structures.c
O=$(subst .c,.o,${C})

all: ${LIBGFS2}

${LIBGFS2}: ${O}
	ar cru libgfs2.a ${O}
	ranlib libgfs2.a

.c.o: $<
	${CC} ${CFLAGS} -o $@ $^

clean:
	rm -f *.o ${LIBGFS2}

#CFLAGS+= -DHELPER_PROGRAM -D_FILE_OFFSET_BITS=64 \
#	 -DGFS2_RELEASE_NAME=\"${RELEASE}\"
