#
# Makefile for GFS2 mkfs
#
.PHONY: all clean install

include ../../make/defines.mk

TARGET1= mkfs.gfs2
TARGET2= gfs2_jadd
TARGET3= gfs2_grow

OBJS=	main.o \
	main_mkfs.o \
	main_grow.o \
	main_jadd.o

CFLAGS += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE
CFLAGS += -D_GNU_SOURCE -DGFS2_RELEASE_NAME=\"${RELEASE}\"
CFLAGS += -Wall -O2 -ggdb
CFLAGS += -I${KERNEL_SRC}/include/
CFLAGS += -I../include/ -I../libgfs2/

LDFLAGS += -L${volidlibdir} -lvolume_id
LDFLAGS += -L../libgfs2 -lgfs2

all: ${TARGET1} ${TARGET2} ${TARGET3}

${TARGET1}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET2}: ${TARGET1}
	ln -s ${TARGET1} ${TARGET2}

${TARGET3}: ${TARGET1}
	ln -s ${TARGET1} ${TARGET3}

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: all
	install -m 0755 ${TARGET1} ${sbindir}
	ln -f ${sbindir}/${TARGET1} ${sbindir}/${TARGET2}
	ln -f ${sbindir}/${TARGET1} ${sbindir}/${TARGET3}

uninstall:
	${UNINSTALL} ${TARGET1} ${TARGET2} ${TARGET3} ${sbindir}

clean:
	rm -f *.o ${TARGET1} ${TARGET2} ${TARGET3}

