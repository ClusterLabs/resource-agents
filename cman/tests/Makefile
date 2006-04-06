

top_srcdir=..

include ${top_srcdir}/make/defines.mk
include ${top_srcdir}/make/flags.mk

CFLAGS+= -g -I${top_srcdir}/config

CFLAGS += -I../lib -L../lib -lcman

TARGET=libtest

all: ${TARGET}

copytobin: all
	cp ${TARGET} ${top_srcdir}/bin

install:
	install -d ${sbindir}
	install user_service ${sbindir}

clean:
	rm -f *.o ${TARGET}


