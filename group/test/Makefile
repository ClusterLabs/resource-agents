TARGETS= client clientd

all: $(TARGETS)

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk

CFLAGS += -I${incdir}

LDFLAGS += -L${libdir}

%: %.o
	$(CC) -o $@ $^ $(LDFLAGS)

clean: generalclean
