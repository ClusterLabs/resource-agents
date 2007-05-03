###############################################################################
###############################################################################
##
##  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

include ../../make/defines.mk

TARGET= gfs2_convert

OBJS= gfs2_convert.o

CFLAGS += -D_FILE_OFFSET_BITS=64 -DHELPER_PROGRAM -DGFS2_RELEASE_NAME=\"${RELEASE}\"
CFLAGS += -Wall -g
CFLAGS += -I${incdir} -I${KERNEL_SRC}/fs/gfs2/ -I${KERNEL_SRC}/include/
CFLAGS += -I../include -I../libgfs2

LDFLAGS += -L../libgfs2 -lgfs2

all: ${TARGET}

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: all
	if [ ! -d ${sbindir} ]; then \
		install -d ${sbindir}; \
	fi
	install -m755 ${TARGET} ${sbindir}

uninstall:
	${UNINSTALL} ${TARGET} ${sbindir}

clean:
	@rm -f *.o ${TARGET}

distclean: clean
	@rm -f {top_srcdir}/make/defines.mk
