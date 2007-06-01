###############################################################################
###############################################################################
##
##  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
##  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

include ../../make/defines.mk

TARGET= gfs2_edit

OBJS=	gfs2hex.o \
	hexedit.o

CFLAGS += -DHELPER_PROGRAM -D_FILE_OFFSET_BITS=64 -DGFS2_RELEASE_NAME=\"${RELEASE}\"
CFLAGS += -ggdb 
CFLAGS += -I${ncursesincdir}
CFLAGS += -I../include -I../libgfs2
CFLAGS += -I${incdir}

LDFLAGS += -L${ncurseslibdir} -lncurses
LDFLAGS += -L../libgfs2/ -lgfs2

all: depends ${TARGET}

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

depends:
	$(MAKE) -C ../libgfs2 all

install: all
	if [ ! -d ${sbindir} ]; then \
		install -d ${sbindir}; \
	fi
	install -m755 ${TARGET} ${sbindir}

uninstall:
	${UNINSTALL} ${TARGET} ${sbindir}

clean:
	rm -f *.o ${TARGET}

.PHONY: all ${TARGET}
