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

TARGET= gfs2_quota

SOURCE=	main.c names.c check.c

INSTALL=install

CFLAGS+= -O2 -DHELPER_PROGRAM -D_FILE_OFFSET_BITS=64 \
	-D_GNU_SOURCE -DGFS2_RELEASE_NAME=\"${RELEASE}\"
LDFLAGS=-L${top_srcdir}/libgfs2 -L${libdir}

INCLUDE= -I${top_srcdir}/include -I${top_srcdir}/config -I${gfs2kincdir} -I${top_srcdir}/libgfs2/

ifneq (${KERNEL_SRC}, )
# Use the kernel tree if patched, otherwise, look where cluster headers
#  should be installed
INCLUDE += $(shell if [ -e ${KERNEL_SRC}/include/linux/gfs2_ondisk.h ]; then \
		echo '-I${KERNEL_SRC}/include'; else \
		echo '-I${incdir}'; fi)
else
INCLUDE += -I${incdir}
endif

#all:
all: ${TARGET}

gfs2_quota: ${SOURCE}
	${CC} ${CFLAGS} ${INCLUDE} ${LDFLAGS} ${SOURCE} ${LOADLIBES} ${LDLIBS} -o $@ -lgfs2

copytobin:
#copytobin: ${TARGET}
#	cp ${TARGET} ${top_srcdir}/bin

install: all
	${INSTALL} -m 0755 ${TARGET} ${sbindir}

clean:
	rm -f *.o ${TARGET}


