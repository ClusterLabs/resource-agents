#
# Makefile for GFS2 mkfs
#
.PHONY: all clean install

MKFS=mkfs.gfs2
INSTALLPATH=/sbin
INCLUDEPATH=-I/home/devel/gfs2-2.6/include/ -I../include/ -I../libgfs2/ -I../config/

INSTALL=install
CC=gcc -c
LD=gcc
CFLAGS=-Wall -O2 -ggdb -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE \
       -D_GNU_SOURCE -DGFS2_RELEASE_NAME=\"2\" ${INCLUDEPATH}
LDFLAGS=-L../libgfs2

H=gfs2_disk_hash.h gfs2_mkfs.h linux_endian.h ondisk.h osi_list.h
C=main.c main_mkfs.c
O=$(subst .c,.o,${C})

all: ${MKFS}

${MKFS}: ${O}
	${LD} ${LDFLAGS} ${O} -o ${@} -lgfs2

.c.o: $<
	${CC} ${CFLAGS} -o $@ $^

install: ${MKFS}
	${INSTALL} -m 0755 -g root -o root ${MKFS} ${INSTALLPATH}

clean:
	rm -f *.o ${MKFS}

