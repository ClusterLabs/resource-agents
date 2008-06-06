TARGET= libgfscontrol

MAKESTATICLIB = 1

OBJS= main.o

include ../../make/defines.mk
include $(OBJDIR)/make/libs.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

CFLAGS += -fPIC
CFLAGS += -I$(S)/../../group/gfs_controld
CFLAGS += -I${incdir}
