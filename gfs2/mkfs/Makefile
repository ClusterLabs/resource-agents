TARGET1= mkfs.gfs2
TARGET2= gfs2_mkfs
TARGET3= gfs2_jadd
TARGET4= gfs2_grow

SBINDIRT=$(TARGET1)
SBINSYMT=$(TARGET2) $(TARGET3) $(TARGET4)

all: depends ${TARGET1} ${TARGET2} ${TARGET3} ${TARGET4}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	main.o \
	main_mkfs.o \
	main_grow.o \
	main_jadd.o

CFLAGS += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -D_GNU_SOURCE
CFLAGS += -I${KERNEL_SRC}/include/
CFLAGS += -I${volidincdir}
CFLAGS += -I$(S)/../include -I$(S)/../libgfs2
CFLAGS += -I${incdir}

LDFLAGS += -L${volidlibdir} -lvolume_id
LDFLAGS += -L../libgfs2 -lgfs2


${TARGET1}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET2}: ${TARGET1}
	ln -sf ${TARGET1} ${TARGET2}

${TARGET3}: ${TARGET1}
	ln -sf ${TARGET1} ${TARGET3}

${TARGET4}: ${TARGET1}
	ln -sf ${TARGET1} ${TARGET4}

depends:
	$(MAKE) -C ../libgfs2 all

clean: generalclean

.PHONY: all ${TARGET1}

-include $(OBJS:.o=.d)
