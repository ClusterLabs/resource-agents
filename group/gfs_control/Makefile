TARGET= gfs_control

SBINDIRT=$(TARGET)

all: depends ${TARGET}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	main.o

CFLAGS += -I$(S)/../libgfscontrol
CFLAGS += -I${incdir}

LDFLAGS += -L../libgfscontrol -lgfscontrol
LDFLAGS += -L${libdir}

LDDEPS += ../libgfscontrol/libgfscontrol.a

${TARGET}: ${OBJS} ${LDDEPS}
	$(CC) -o $@ $^ $(LDFLAGS)

clean: generalclean

depends:
	$(MAKE) -C ../libgfscontrol all

-include $(OBJS:.o=.d)
