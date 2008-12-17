TARGETS= testcmanquorum1 testcmanquorum2

all: depends ${TARGETS}

include ../../../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

CFLAGS += -I../include
CFLAGS += -I${incdir}

LDFLAGS += -L../lib -lcmanquorum
LDFLAGS += -L${libdir} 
LDFLAGS += -L${corosynclibdir} -lcoroutil

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

depends:
	$(MAKE) -C ../lib all

clean: generalclean

-include $(OBJS:.o=.d)
