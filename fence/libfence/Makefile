TARGET= libfence

OBJS=	agent.o

include ../../make/defines.mk
include $(OBJDIR)/make/libs.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

CFLAGS += -fPIC
CFLAGS += -I${ccsincdir}
CFLAGS += -I${incdir}

LDFLAGS += -L${ccslibdir} -lccs
LDFLAGS += -L${libdir}
