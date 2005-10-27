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
UTIL=util-linux-2.12r-dt1

TARGET= mount.gfs2

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


all: ${TARGET}

mount.gfs2.o: mount.gfs2.c
	${CC} ${CFLAGS} ${INCLUDE} -c -o $@ $<

ondisk.o: ondisk.c
	${CC} ${CFLAGS} ${INCLUDE} -c -o $@ $<

mount.gfs2: mount.gfs2.o ondisk.o ${UTIL}/${UTIL}/mount/libmount.a
	${CC} ${LDFLAGS} -o $@ $^ -lblkid -luuid

${UTIL}/${UTIL}.tar.gz:
	mkdir -p ${UTIL}
#	cd ${UTIL} && cp ~/${UTIL}.tar.gz .
	cd ${UTIL} && wget http://people.redhat.com/teigland/${UTIL}.tar.gz

${UTIL}/${UTIL}: ${UTIL}/${UTIL}.tar.gz
	cd ${UTIL} && tar -xzf ${UTIL}.tar.gz
	touch ${UTIL}/${UTIL}

${UTIL}/${UTIL}/mount/libmount.a: ${UTIL}/${UTIL}
	cd ${UTIL}/${UTIL} && ./configure && (cd lib; make) && cd mount && make

install: mount.gfs2
	install mount.gfs2 ${sbindir}

copytobin: ${TARGET}
	cp ${TARGET} ${top_srcdir}/bin

clean:
	rm -f *.o ${TARGET} mount.gfs2

reallyclean: clean
	rm -rf ${UTIL}

