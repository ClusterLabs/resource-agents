top_srcdir=..
all: ${TARGET}

include ${top_srcdir}/make/defines.mk

TARGET= gfs2_debug

SOURCE=	\
	block_device.c \
	basic.c \
	main.c \
	ondisk.c \
	readfile.c \
	util.c

CFLAGS+= -DHELPER_PROGRAM -D_FILE_OFFSET_BITS=64

INCLUDE= -I${top_srcdir}/include -I${top_srcdir}/config \
	-I${gfs2kincdir} -I${incdir}

ifneq (${KERNEL_SRC}, )
# Use the kernel tree if patched, otherwise, look where cluster headers
#  should be installed
INCLUDE += $(shell if [ -e ${KERNEL_SRC}/include/linux/gfs2_ondisk.h ]; then \
		echo '-I${KERNEL_SRC}/include'; else \
		echo '-I${incdir}'; fi)
else
INCLUDE += -I${incdir}
endif

LDFLAGS+= -L${libdir}


gfs2_debug: ${SOURCE}
	${CC} ${CFLAGS} ${INCLUDE} ${LDFLAGS} ${SOURCE} ${LDLIBS} -o $@

install: all
	if [ ! -d ${sbindir} ]; then \
		install -d ${sbindir}; \
	fi
	install -m755 ${TARGET} ${sbindir}

clean:
	rm -f *.o ${TARGET}


