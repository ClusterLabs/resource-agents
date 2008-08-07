TARGET= libccscompat

MAKESTATICLIB = 1

include ../../../make/defines.mk
include $(OBJDIR)/make/libs.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

CFLAGS += -D_FILE_OFFSET_BITS=64
CFLAGS += -fPIC
CFLAGS += -I$(SRCDIR)/ccs/daemon
CFLAGS += -I${incdir}
