TARGET= gfs2_quota

SBINDIRT=$(TARGET)

all: depends ${TARGET}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	main.o \
	names.o \
	check.o

CFLAGS += -DHELPER_PROGRAM -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE
CFLAGS += -I${KERNEL_SRC}/fs/gfs2/ -I${KERNEL_SRC}/include/
CFLAGS += -I$(S)/../include -I$(S)/../libgfs2
CFLAGS += -I${incdir}

LDFLAGS += -L../libgfs2 -lgfs2
LDFLAGS += -L${libdir}

LDDEPS += ../libgfs2/libgfs2.a

${TARGET}: ${OBJS} ${LDDEPS}
	$(CC) -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C ../libgfs2 all

clean: generalclean

-include $(OBJS:.o=.d)
