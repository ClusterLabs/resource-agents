TARGETS= client libtest sysman sysmand

all: depends ${TARGETS}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk

CFLAGS += -I${cmanincdir}
CFLAGS += -I${incdir}

LDFLAGS += -L${cmanlibdir} -lcman
LDFLAGS += -L${libdir}

depends:
	$(MAKE) -C ../lib all

%: %.o
	$(CC) -o $@ $^ $(LDFLAGS)

install:

clean: generalclean
