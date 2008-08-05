TARGET= config_ccs.lcrso

LCRSOT=$(TARGET)

all: depends ${TARGET}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

CFLAGS += -fPIC
CFLAGS += -I$(S)/../libccscompat
CFLAGS += -I${cmanincdir}/../daemon
CFLAGS += -I${incdir}

LDFLAGS += -L../libccscompat -lccscompat
LDFLAGS += -L${libdir}

LDDEPS += ../libccscompat/libccscompat.a

OBJS=	config.o

${TARGET}: ${OBJS} ${LDDEPS}
	$(CC) -shared -Wl,-soname,$@ -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C ../libccscompat all

clean: generalclean 

-include $(OBJS:.o=.d)
