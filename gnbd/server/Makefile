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

TARGET= gnbd_serv gnbd_clusterd

top_srcdir=..
include ${top_srcdir}/make/defines.mk

CLU_SOURCE= gnbd_clusterd.c $(top_srcdir)/utils/gnbd_utils.c

CLU_LIBS= ${LDLIBS} -L${libdir} -lmagma -ldl

SRV_SOURCE= gnbd_serv.c local_req.c extern_req.c device.c gserv.c fence.c \
	$(top_srcdir)/utils/trans.c $(top_srcdir)/utils/gnbd_utils.c

INCLUDE= -I$(top_srcdir)/include -I$(top_srcdir)/utils \
	-I${top_srcdir}/config -I${incdir}

ifneq (${KERNEL_SRC}, )
INCLUDE += -I${KERNEL_SRC}/include
endif

CFLAGS+= -O2 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE \
	-DGNBD_RELEASE_NAME=\"${RELEASE}\"

all: ${TARGET}

gnbd_clusterd: ${CLU_SOURCE}
	${CC} ${CFLAGS} ${LDFLAGS} ${CLU_SOURCE} ${LOADLIBES} ${CLU_LIBS} -o $@

gnbd_serv: ${SRV_SOURCE}
	${CC} ${CFLAGS} ${LDFLAGS} ${SRV_SOURCE} ${LOADLIBES} ${LDLIBS} -o $@

copytobin: all
	cp ${TARGET} ${top_srcdir}/bin

clean:
	rm -f *.o ${TARGET}
