TARGET1= mount.gfs2

all: ${TARGET1}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk

OBJS1=	mount.gfs2.o \
	ondisk1.o \
	util.o \
	mtab.o

CFLAGS += -DHELPER_PROGRAM -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE
CFLAGS += -I${gfskincdir}
CFLAGS += -I${KERNEL_SRC}/fs/gfs2/ -I${KERNEL_SRC}/include/
CFLAGS += -I$(SRCDIR)/group/libgfscontrol
CFLAGS += -I$(S)/../include
CFLAGS += -I${incdir}

LDFLAGS += -L../../group/libgfscontrol -lgfscontrol

${TARGET1}: ${OBJS1}
	$(CC) -o $@ $^ $(LDFLAGS)

install: all
	mkdir -p ${DESTDIR}/sbin
	install ${TARGET1} ${DESTDIR}/sbin

uninstall:
	${UNINSTALL} ${TARGET1} ${DESTDIR}/sbin

clean: generalclean

-include $(OBJS1:.o=.d)
