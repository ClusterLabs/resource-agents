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

TARGET= ccs_tool

OBJS=	ccs_tool.o \
	update.o \
	upgrade.o \
	old_parser.o \
	editconf.o

CFLAGS += -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE
CFLAGS += -I${ccsincdir} -I${cmanincdir} `xml2-config --cflags`
CFLAGS += -I../include
CFLAGS += -I${incdir}

ifeq ($(DEBUG),y)
CFLAGS += -O0 -DDEBUG -g
else
CFLAGS += -O2
endif

LDFLAGS += -L${ccslibdir} -L${cmanlibdir} -lccs -lcman
LDFLAGS += `xml2-config --libs` -L${libdir} -ldl

all: depends ${TARGET}

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

depends:
	$(MAKE) -C ../lib all

clean:
	rm -f *.o ccs_tool *~

install: all
	install -d ${sbindir}
	install ccs_tool ${sbindir}

uninstall:
	${UNINSTALL} ccs_tool ${sbindir}

.PHONY: all ${TARGET}
