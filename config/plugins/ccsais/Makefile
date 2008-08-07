TARGET= config_ccs.lcrso

LCRSOT=$(TARGET)

all: depends ${TARGET}

include ../../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

CFLAGS += -fPIC
CFLAGS += -I$(SRCDIR)/config/libs/libccscompat
CFLAGS += -I${cmanincdir}/../daemon
CFLAGS += -I${incdir}

LDFLAGS += -L$(OBJDIR)/config/libs/libccscompat -lccscompat
LDFLAGS += -L${libdir}

LDDEPS += $(OBJDIR)/config/libs/libccscompat/libccscompat.a

OBJS=	config.o

${TARGET}: ${OBJS} ${LDDEPS}
	$(CC) -shared -Wl,-soname,$@ -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C $(OBJDIR)/config/libs/libccscompat all

clean: generalclean 

-include $(OBJS:.o=.d)
