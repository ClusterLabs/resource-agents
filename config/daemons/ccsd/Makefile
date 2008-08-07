TARGET= ccsd

SBINDIRT=$(TARGET)

all: depends ${TARGET}

include ../../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	ccsd.o \
	cnx_mgr.o \
	cluster_mgr.o \
	misc.o \
	globals.o

CFLAGS += -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE
CFLAGS += -I${cmanincdir} `xml2-config --cflags` -I${corosyncincdir}
CFLAGS += -I$(S)
CFLAGS += -I${incdir}

LDFLAGS += -L${cmanlibdir} -lcman
LDFLAGS += -L${corosynclibdir} -llogsys
LDFLAGS += `xml2-config --libs` -lpthread
LDFLAGS += -L${libdir}

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C $(OBJDIR)/cman/lib all

clean: generalclean

-include $(OBJS:.o=.d)
