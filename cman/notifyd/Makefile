TARGET= cmannotifyd

SBINDIRT=${TARGET}

all: depends ${TARGET}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

CFLAGS += -D_GNU_SOURCE -DSBINDIR=\"${sbindir}\"
CFLAGS += -I${ccsincdir} -I${cmanincdir} -I${logtincdir}
CFLAGS += -I${incdir}

LDFLAGS += -L${cmanlibdir} -L${ccslibdir} -L${logtlibdir} -lcman -lccs -llogthread
LDFLAGS += -L${libdir}

OBJS=	main.o

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C ../lib all

clean: generalclean

-include $(OBJS:.o=.d)
