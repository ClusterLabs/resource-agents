TARGETS= filecon2_server filecon2_client

all: $(TARGETS)

include ../../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk

CFLAGS += -O3
CFLAGS += -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64
CFLAGS += -I../../include
CFLAGS += -I${incdir}

LDFLAGS += -L${libdir}

%: %.o
	$(CC) -o $@ $^ $(LDFLAGS)

clean: generalclean
