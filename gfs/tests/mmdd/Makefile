TARGETS= mmdd sfdd

all: $(TARGETS)

include ../../../make/defines.mk
include $(OBJDIR)/make/clean.mk

CFLAGS += -D_GNU_SOURCE
CFLAGS += -I${incdir}

LDFLAGS += -L${libdir}

mmdd.o: mmdd.c
	$(CC) $(CFLAGS) -D_FILE_OFFSET_BITS=64 -c -o $@ $<

sfdd.o: mmdd.c
	$(CC) $(CFLAGS) -DUSE_SENDFILE -c -o $@ $<

%: %.o
	$(CC) -o $@ $^ $(LDFLAGS)

clean: generalclean

-include mmdd.d
-include sfdd.d
