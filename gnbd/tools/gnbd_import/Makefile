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

TARGET= gnbd_import

top_srcdir=../..
include ${top_srcdir}/make/defines.mk

SOURCE= gnbd_import.c $(top_srcdir)/utils/gnbd_utils.c \
	$(top_srcdir)/client/monitor_req.c $(top_srcdir)/utils/trans.c

INCLUDE= -I$(top_srcdir)/include -I$(top_srcdir)/client \
	-I$(top_srcdir)/server -I$(top_srcdir)/utils \
	-I${top_srcdir}/config

ifneq (${KERNEL_SRC}, )
INCLUDE += -I${KERNEL_SRC}/include
endif

CFLAGS+= -O2 -DGNBD_RELEASE_NAME=\"${RELEASE}\"

all: ${TARGET}

gnbd_import: ${SOURCE}
	${CC} ${CFLAGS} ${LDFLAGS} ${SOURCE} ${LOADLIBES} ${LDLIBS} -o $@

copytobin: all
	cp ${TARGET} ${top_srcdir}/bin

clean:
	rm -f *.o ${TARGET}
