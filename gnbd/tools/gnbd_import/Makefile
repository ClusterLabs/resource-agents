TARGET= gnbd_import

SBINDIRT=$(TARGET)

all: depends ${TARGET}

include ../../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	gnbd_import.o \
	../../utils/gnbd_utils.o \
	../../client/monitor_req.o \
	../../utils/trans.o

CFLAGS += -I${gnbdkincdir} -I${cmanincdir}
CFLAGS += -I$(S)/../../include -I$(S)/../../server
CFLAGS += -I$(S)/../../utils -I$(S)/../../client
CFLAGS += -I${incdir}

LDFLAGS += -L${libdir} -L${cmanlibdir} -lcman

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C ../../utils all
	$(MAKE) -C ../../client monitor_req.o

clean: generalclean

-include $(OBJS:.o=.d)
