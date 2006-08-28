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
UNINSTALL=${top_srcdir}/scripts/uninstall.pl

TARGET1= mount.gfs2
TARGET2= umount.gfs2

CFLAGS+= -O2 -DHELPER_PROGRAM -D_FILE_OFFSET_BITS=64 \
	-DGFS2_RELEASE_NAME=\"${RELEASE}\" -D_GNU_SOURCE

INCLUDE= -I${top_srcdir}/include \
	  -I${top_srcdir}/config \
	  -I${gfs2kincdir} \
	  -I${incdir} \
	  -I../../gfs-kernel/src/gfs

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

ondisk1.o: ondisk1.c
	${CC} ${CFLAGS} ${INCLUDE} -c -o $@ $<

ondisk2.o: ondisk2.c
	${CC} ${CFLAGS} ${INCLUDE} -c -o $@ $<

util.o: util.c
	${CC} ${CFLAGS} ${INCLUDE} -c -o $@ $<

mtab.o: mtab.c
	${CC} ${CFLAGS} ${INCLUDE} -c -o $@ $<

umount.gfs2.o: umount.gfs2.c
	${CC} ${CFLAGS} ${INCLUDE} -c -o $@ $<

mount.gfs2: mount.gfs2.o ondisk1.o ondisk2.o util.o mtab.o
	${CC} ${LDFLAGS} -o $@ $^

umount.gfs2: umount.gfs2.o ondisk1.o ondisk2.o util.o mtab.o
	${CC} ${LDFLAGS} -o $@ $^

install: all
	install mount.gfs2 ${sbindir}
	install umount.gfs2 ${sbindir}

uninstall:
	${UNINSTALL} mount.gfs2 umount.gfs2 mount.gfs umount.gfs ${sbindir}

clean:
	rm -f *.o ${TARGET1} ${TARGET2}

