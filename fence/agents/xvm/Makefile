###############################################################################
###############################################################################
##
##  Copyright (C) 2006 Red Hat, Inc.
##  
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

include ../../../make/defines.mk

TARGET1= fence_xvm
TARGET2= fence_xvmd


OBJS1=	fence_xvm.o \
	ip_lookup.o

OBJS2=	fence_xvmd.o \
	virt.o \
	options-ccs.o \
	vm_states.o \
	xml.o

SHAREDOBJS=	mcast.o \
		simple_auth.o \
		tcp.o \
		options.o \
		debug.o

CFLAGS += -DFENCE_RELEASE_NAME=\"${RELEASE}\" -D_GNU_SOURCE
CFLAGS += -Wall -Werror -Wstrict-prototypes -Wshadow -ggdb
CFLAGS += -I${ccsincdir} -I${cmanincdir} -I${openaisincdir}
CFLAGS += -I${virtincdir} -I${nssincdir} -I${nsprincdir}
CFLAGS += -I${incdir}
CFLAGS += `xml2-config --cflags`

LDFLAGS += -L${ccslibdir} -L${cmanlibdir} -L${dlmlibdir}
LDFLAGS += -L${nsslibdir} -L${nsprlibdir} -L${libdir}
LDFLAGS += -lccs -lcman -ldlm -lnss3 -lnspr4 

EXTRA_LDFLAGS += -L${virtlibdir} -lvirt -L${openaislibdir} -lSaCkpt
EXTRA_LDFLAGS += `xml2-config --libs`

all: ${TARGET1} ${TARGET2}

${TARGET1}: ${SHAREDOBJS} ${OBJS1}
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET2}: ${SHAREDOBJS} ${OBJS2}
	$(CC) -o $@ $^ $(LDFLAGS) $(EXTRA_LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f ${TARGET1} ${TARGET2} *~ *.o

install: all
	if [ ! -d ${sbindir} ]; then \
		install -d ${sbindir}; \
	fi
	install -m755 ${TARGET1} ${TARGET2} ${sbindir}

uninstall:
	${UNINSTALL} ${TARGET1} ${TARGET2} ${sbindir}
