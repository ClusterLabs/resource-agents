TARGET= libccs

INCDIRT=ccs.h

OBJS=	$(TARGET).o \
	xpathlite.o \
	fullxpath.o \
	extras.o

include ../../../make/defines.mk
include $(OBJDIR)/make/libs.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

CFLAGS += -fPIC
CFLAGS += -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64
CFLAGS += -I${corosyncincdir} -I${logtincdir} `xml2-config --cflags`
CFLAGS += -I${incdir}

LDFLAGS += -L${corosynclibdir} -lconfdb
LDFLAGS += `xml2-config --libs`
LDFLAGS += -L${libdir}
