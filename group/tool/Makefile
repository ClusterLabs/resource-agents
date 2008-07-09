TARGET= group_tool

SBINDIRT=$(TARGET)

all: depends ${TARGET}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	main.o

CFLAGS += -I$(S) -I$(S)/../daemon/ -I$(S)/../lib/
CFLAGS += -I${dlmcontrolincdir}
CFLAGS += -I$(S)/../../fence/libfenced
CFLAGS += -I$(S)/../libgfscontrol
CFLAGS += -I${incdir}

LDFLAGS += -L../lib -lgroup
LDFLAGS += -L${dlmcontrollibdir} -ldlmcontrol
LDFLAGS += -L../../fence/libfenced -lfenced
LDFLAGS += -L../libgfscontrol -lgfscontrol

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C ../lib all

clean: generalclean

.PHONY: all ${TARGET}
