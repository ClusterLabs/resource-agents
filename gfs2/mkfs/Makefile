#
# Makefile for GFS2 mkfs
#
.PHONY: all clean install

MKFS=mkfs.gfs2
INSTALLPATH=/sbin
INCLUDEPATH=-I/home/steve/linux-2.6/include/

INSTALL=install
CC=gcc -c
LD=gcc
CFLAGS=-Wall -O2 -ggdb -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE \
       -D_GNU_SOURCE -DGFS2_RELEASE_NAME=\"2\" ${INCLUDEPATH}
LDFLAGS=

H=gfs2_disk_hash.h  gfs2_mkfs.h  iddev.h  linux_endian.h  ondisk.h  osi_list.h
C=main.c buf.c device_geometry.c fs_geometry.c locking.c main_mkfs.c \
  misc.c ondisk.c structures.c fs_ops.c size.c iddev.c
O=$(subst .c,.o,${C})

all: ${MKFS}

${MKFS}: ${O}
	${LD} ${LDFLAGS} ${O} -o ${@}

.c.o: $<
	${CC} ${CFLAGS} -o $@ $^

install: ${MKFS}
	${INSTALL} -m 0755 -g root -o root ${MKFS} ${INSTALLPATH}

clean:
	rm -f *.o ${MKFS}

