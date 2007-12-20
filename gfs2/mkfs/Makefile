#
# Makefile for GFS2 mkfs
#
.PHONY: all clean install

include ../../make/defines.mk

TARGET1= mkfs.gfs2
TARGET2= gfs2_mkfs
TARGET3= gfs2_jadd
TARGET4= gfs2_grow

OBJS=	main.o \
	main_mkfs.o \
	main_grow.o \
	main_jadd.o

CFLAGS += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -D_GNU_SOURCE
CFLAGS += -I${KERNEL_SRC}/include/
CFLAGS += -I$(S)/../include -I$(S)/../libgfs2
CFLAGS += -I${incdir}

LDFLAGS += -L${volidlibdir} -lvolume_id
LDFLAGS += -L../libgfs2 -lgfs2

all: depends ${TARGET1} ${TARGET2} ${TARGET3} ${TARGET4}

${TARGET1}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET2}: ${TARGET1}
	ln -sf ${TARGET1} ${TARGET2}

${TARGET3}: ${TARGET1}
	ln -sf ${TARGET1} ${TARGET3}

${TARGET4}: ${TARGET1}
	ln -sf ${TARGET1} ${TARGET4}

%.o: $(S)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

depends:
	$(MAKE) -C ../libgfs2 all

install: all
	install -m 0755 ${TARGET1} ${sbindir}
	cd ${sbindir}; \
	 ln -sf ${TARGET1} ${TARGET2}; \
	 ln -sf ${TARGET1} ${TARGET3}; \
	 ln -sf ${TARGET1} ${TARGET4}

uninstall:
	${UNINSTALL} ${TARGET1} ${TARGET2} ${TARGET3} ${TARGET4} ${sbindir}

clean:
	rm -f *.o ${TARGET1} ${TARGET2} ${TARGET3} ${TARGET4}

.PHONY: all ${TARGET1}
