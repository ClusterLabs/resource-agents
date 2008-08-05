TARGET= dlm_tool

SBINDIRT=$(TARGET)

all: depends ${TARGET}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	main.o

CFLAGS += -I${dlmincdir} -I${dlmcontrolincdir}
CFLAGS += -I$(SRCDIR)/group/dlm_controld/
CFLAGS += -I${incdir}
CFLAGS += -I${KERNEL_SRC}/include/

LDFLAGS += -L${dlmlibdir} -L${dlmcontrollibdir} -ldlm -ldlmcontrol
LDFLAGS += -L${libdir}

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

clean: generalclean

depends:
	$(MAKE) -C ../libdlm all
	$(MAKE) -C ../libdlmcontrol all

-include $(OBJS:.o=.d)
