TARGET1= gfs_mkfs
TARGET2= mkfs.gfs

SBINDIRT=$(TARGET1)
SBINSYMT=$(TARGET2)

all: depends ${TARGET1} ${TARGET2}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	device_geometry.o \
	fs_geometry.o \
	locking.o \
	main.o \
	ondisk.o \
	structures.o

CFLAGS += -DHELPER_PROGRAM -D_FILE_OFFSET_BITS=64
CFLAGS += -I${gfskincdir} -I${volidincdir}
CFLAGS += -I$(S)/../include -I$(S)/../libgfs
CFLAGS += -I${incdir}

LDFLAGS += -L${volidlibdir} -lvolume_id
LDFLAGS += -L../libgfs -lgfs

LDDEPS += ../libgfs/libgfs.a

${TARGET1}: ${OBJS} ${LDDEPS}
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET2}: ${TARGET1}
	ln -sf ${TARGET1} ${TARGET2}

depends:
	$(MAKE) -C ../libgfs all

clean: generalclean

-include $(OBJS:.o=.d)
