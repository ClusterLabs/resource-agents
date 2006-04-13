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
TARGET= gnbd_recvd gnbd_monitor

top_srcdir=..
include ${top_srcdir}/make/defines.mk

RECV_SOURCE= gnbd_recvd.c $(top_srcdir)/utils/trans.c \
	$(top_srcdir)/utils/gnbd_utils.c

MONITOR_SRC= gnbd_monitor.c monitor_req.c $(top_srcdir)/utils/trans.c \
	$(top_srcdir)/utils/gnbd_utils.c

INCLUDE= -I$(top_srcdir)/include -I$(top_srcdir)/server -I$(top_srcdir)/utils \
	-I${top_srcdir}/config -I${gnbdkincdir} -I${incdir}

ifneq (${KERNEL_SRC}, )
# Use the kernel tree if patched, otherwise, look where cluster headers
#  should be installed -- which is incdir, which is already included
INCLUDE += $(shell if [ -e ${KERNEL_SRC}/include/linux/gnbd.h ]; then \
		echo '-I${KERNEL_SRC}/include'; fi)
endif

LDLIBS+= -L${libdir} -ldl -lpthread

CFLAGS+= -O2 -DGNBD_RELEASE_NAME=\"${RELEASE}\"

all: ${TARGET}

gnbd_recvd: ${RECV_SOURCE}
	${CC} ${CFLAGS} ${LDFLAGS} ${RECV_SOURCE} ${LOADLIBES} ${LDLIBS} -o $@

gnbd_monitor: ${MONITOR_SRC}
	${CC} ${CFLAGS} ${LDFLAGS} ${MONITOR_SRC} ${LOADLIBES} ${LDLIBS} -o $@

copytobin: all
	cp ${TARGET} ${top_srcdir}/bin

clean:
	rm -f *.o *~ ${TARGET}
