#
# Makefile for GFS2 mkfs
#
.PHONY: all clean install

top_srcdir=..

include ${top_srcdir}/make/defines.mk

MKFS=mkfs.gfs2
INSTALLPATH=/sbin
INCLUDEPATH=-I${KERNEL_SRC}/include/ -I${top_srcdir}/include/ -I${top_srcdir}/libgfs2/ -I${top_srcdir}/config/ 
#When udev's libvolume_id gets gfs and gfs2 incorporated:
#INCLUDEPATH=$INCLUDEPATH -I${top_srcdir}/libvolume_id

INSTALL=install
CC=gcc -c
LD=gcc
CFLAGS=-Wall -O2 -ggdb -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE \
       -D_GNU_SOURCE -DGFS2_RELEASE_NAME=\"2\" ${INCLUDEPATH}
LDFLAGS=-L${top_srcdir}/libgfs2 
#When udev's libvolume_id gets gfs and gfs2 incorporated:
#LDFLAGS=$LDFLAGS -L${top_srcdir}/libvolume_id -static

H=gfs2_disk_hash.h gfs2_mkfs.h linux_endian.h ondisk.h osi_list.h
C=main.c main_mkfs.c
O=$(subst .c,.o,${C})

all: ${MKFS}

${MKFS}: ${O}
	${LD} ${LDFLAGS} ${O} -o ${@} -lgfs2
#When udev's libvolume_id gets gfs and gfs2 incorporated add:
#-lvolume_id

.c.o: $<
	${CC} ${CFLAGS} -o $@ $^

install: ${MKFS}
	${INSTALL} -m 0755 -g root -o root ${MKFS} ${INSTALLPATH}

clean:
	rm -f *.o ${MKFS}

