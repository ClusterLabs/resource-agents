OBJS=	member_cman.o \
	trans.o \
	gnbd_utils.o

all: ${OBJS}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

CFLAGS += -I${cmanincdir}
CFLAGS += -I$(S)/../include

clean: generalclean

-include $(OBJS:.o=.d)
