

top_srcdir=..

include ${top_srcdir}/make/defines.mk
include ${top_srcdir}/make/flags.mk

CFLAGS+= -g -I${top_srcdir}/config

ifneq (${KERNEL_SRC}, )
CFLAGS += -I${KERNEL_SRC}/include/cluster
else
CFLAGS += -I/usr/include/linux/cluster
endif

TARGET=user_service sysman sysmand client qwait

all: ${TARGET}

copytobin: all
	cp ${TARGET} ${top_srcdir}/bin

user_service: user_service.o
	$(CC) $(LDFLAGS) -o $@ $^ -lpthread

user_service.o: user_service.c
	$(CC) $(CFLAGS) -c -o $@ $<

install:
	install -d ${sbindir}
	install user_service ${sbindir}

clean:
	rm -f *.o ${TARGET}


