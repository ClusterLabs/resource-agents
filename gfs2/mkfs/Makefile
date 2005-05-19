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

top_srcdir=..
include ${top_srcdir}/make/defines.mk

TARGET= gfs2_mkfs

SOURCE=	\
	buf.c \
	device_geometry.c \
	fs_geometry.c \
	fs_ops.c \
	live.c \
	locking.c \
	main.c \
	main_grow.c \
	main_jadd.c \
	main_mkfs.c \
	main_shrink.c \
	misc.c \
	ondisk.c \
	structures.c

CFLAGS+= -O2 -DHELPER_PROGRAM -D_FILE_OFFSET_BITS=64 \
	-DGFS2_RELEASE_NAME=\"${RELEASE}\" -D_GNU_SOURCE

INCLUDE= -I${top_srcdir}/include -I${top_srcdir}/config \
	-I${gfs2kincdir} -I${iddevincdir} -I${incdir}

ifneq (${KERNEL_SRC}, )
# Use the kernel tree if patched, otherwise, look where cluster headers
#  should be installed
INCLUDE += $(shell if [ -e ${KERNEL_SRC}/include/linux/gfs2_ondisk.h ]; then \
		echo '-I${KERNEL_SRC}/include'; else \
		echo '-I${incdir}'; fi)
else
INCLUDE += -I${incdir}
endif

LDFLAGS+= -L${iddevlibdir} -L${libdir}
LOADLIBES+= -liddev

all: ${TARGET}

gfs2_mkfs: ${SOURCE}
	${CC} ${CFLAGS} ${INCLUDE} ${LDFLAGS} ${SOURCE} ${LOADLIBES} ${LDLIBS} -o $@

copytobin: ${TARGET}
	cp ${TARGET} ${top_srcdir}/bin

clean:
	rm -f *.o ${TARGET}


