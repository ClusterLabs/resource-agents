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

TARGET= gfs_grow

SOURCE=	main.c \
	ondisk.c

CFLAGS= -DHELPER_PROGRAM -D_FILE_OFFSET_BITS=64 \
	-DGFS_RELEASE_NAME=\"${RELEASE}\"

INCLUDE= -I${top_srcdir}/include -I${top_srcdir}/config -I${incdir}

ifneq (${KERNEL_SRC}, )
INCLUDE += -I${KERNEL_SRC}/include
endif

LDFLAGS+= -L${libdir}
LOADLIBS+= -liddev

all: ${TARGET}

gfs_grow:
	${CC} ${CFLAGS} ${INCLUDE} main.c ondisk.c ${LDFLAGS} ${LOADLIBS} -o $@

copytobin: all
	cp ${TARGET} ${top_srcdir}/bin

clean:
	rm -f *.o ${TARGET}
