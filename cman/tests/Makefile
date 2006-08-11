

top_srcdir=..

include ${top_srcdir}/make/defines.mk
include ${top_srcdir}/make/flags.mk

CFLAGS+= -g -I${top_srcdir}/config -I ../lib

LDFLAGS+= -L../lib -lcman

TARGET=client libtest sysman sysmand

all: ${TARGET}

.c:
	$(CC) $(CFLAGS) $(OUTPUT_OPTION) $< $(LDFLAGS)

install: all

clean:
	rm -f *.o ${TARGET}


