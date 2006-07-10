#
# Makefile for GFS2 mkfs
#
.PHONY: all clean install copytobin

top_srcdir=..

include ${top_srcdir}/make/defines.mk

MKFS=mkfs.gfs2
JADD=gfs2_jadd
INSTALLPATH=/sbin
INCLUDEPATH=-I${KERNEL_SRC}/include/ -I${top_srcdir}/include/ -I${top_srcdir}/libgfs2/ -I${top_srcdir}/config/

INSTALL=install
CC=gcc -c
LD=gcc
CFLAGS=-Wall -O2 -ggdb -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE \
       -D_GNU_SOURCE -DGFS2_RELEASE_NAME=\"2\" ${INCLUDEPATH}
LDFLAGS=-L${top_srcdir}/libgfs2 -L${libdir}

H=gfs2_disk_hash.h gfs2_mkfs.h linux_endian.h ondisk.h osi_list.h
C=main.c main_mkfs.c main_jadd.c
O=$(subst .c,.o,${C})

all: ${MKFS} ${JADD}

${MKFS}: ${O}
	${LD} ${LDFLAGS} ${O} -o ${@} -lgfs2 -lvolume_id

${JADD}: ${MKFS}
	ln -s ${MKFS} ${JADD}

.c.o: $<
	${CC} ${CFLAGS} -o $@ $^

copytobin: ${MKFS}
	cp ${MKFS} ${top_srcdir}/bin
	ln -sf ${MKFS} ${top_srcdir}/bin/${JADD}

install: ${MKFS}
	${INSTALL} -m 0755 -g root -o root ${MKFS} ${INSTALLPATH}
	ln -sf ${MKFS} ${INSTALLPATH}/${JADD}

clean:
	rm -f *.o ${MKFS} ${JADD}

