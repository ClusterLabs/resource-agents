include ../../make/defines.mk

CFLAGS+= -g -I${cmanincdir}

LDFLAGS+= -L${cmanlibdir} -lcman

TARGET=client libtest sysman sysmand

all: ${TARGET}

.c:
	$(CC) $(CFLAGS) $(OUTPUT_OPTION) $< $(LDFLAGS)

install: all

clean:
	rm -f *.o ${TARGET}

