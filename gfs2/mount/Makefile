###############################################################################
###############################################################################
##
##  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

top_srcdir=..
include ${top_srcdir}/make/defines.mk

TARGET1= mount.gfs2
TARGET2= umount.gfs2

CFLAGS+= -O2 -DHELPER_PROGRAM -D_FILE_OFFSET_BITS=64 \
	-DGFS2_RELEASE_NAME=\"${RELEASE}\" -D_GNU_SOURCE

INCLUDE= -I${top_srcdir}/include -I${top_srcdir}/config \
	-I${gfs2kincdir} -I${incdir}

ifneq (${KERNEL_SRC}, )
# Use the kernel tree if patched, otherwise, look where cluster headers
#  should be installed
INCLUDE += $(shell if [ -e ${KERNEL_SRC}/include/linux/gfs2_ondisk.h ]; then \
	echo '-I${KERNEL_SRC}/include'; else \
	echo '-I${incdir}'; fi)
else
INCLUDE += -I${incdir}
endif


all: ${TARGET1} ${TARGET2}

mount.gfs2.o: mount.gfs2.c
	${CC} ${CFLAGS} ${INCLUDE} -c -o $@ $<

ondisk.o: ondisk.c
	${CC} ${CFLAGS} ${INCLUDE} -c -o $@ $<

umount.gfs2.o: umount.gfs2.c
	${CC} ${CFLAGS} ${INCLUDE} -c -o $@ $<

mount.gfs2: mount.gfs2.o ondisk.o
	${CC} ${LDFLAGS} -o $@ $^

umount.gfs2: umount.gfs2.o
	${CC} ${LDFLAGS} -o $@ $^

install: ${TARGET1} ${TARGET2}
	install ${TARGET1} ${sbindir}
	install ${TARGET2} ${sbindir}

copytobin: ${TARGET1} ${TARGET2}
	cp ${TARGET1} ${top_srcdir}/bin
	cp ${TARGET2} ${top_srcdir}/bin

clean:
	rm -f *.o ${TARGET1} ${TARGET2}

