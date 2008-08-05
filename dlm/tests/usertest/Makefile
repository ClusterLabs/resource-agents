TARGETS= dlmtest asttest lstest pingtest lvb \
	 dlmtest2 flood alternate-lvb joinleave threads

all: depends ${TARGETS}

include ../../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk

CFLAGS += -D_REENTRANT
CFLAGS += -I${dlmincdir}
CFLAGS += -I${incdir}

LDFLAGS += -L${dlmlibdir} -ldlm -lpthread
LDFLAGS += -L${libdir}

depends:
	$(MAKE) -C ../../libdlm all

%: %.o
	$(CC) -o $@ $^ $(LDFLAGS)

clean: generalclean
