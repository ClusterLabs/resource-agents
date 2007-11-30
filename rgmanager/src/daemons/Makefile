###############################################################################
###############################################################################
##
##  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

include ../../../make/defines.mk

TARGET1= clurgmgrd
TARGET2= clurmtabd
TARGET3= rg_test
TARGET4= dtest

OBJS1=	depends.o \
	fo_domain.o \
	groups.o \
	main.o \
	reslist.o \
	resrules.o \
	restree.o \
	rg_event.o \
	rg_forward.o \
	rg_locks.o \
	rg_queue.o \
	rg_state.o \
	rg_thread.o \
	restart_counter.o \
	service_op.o \
	slang_event.o \
	event_config.o \
	watchdog.o

OBJS2=	clurmtabd.o \
	clurmtabd_lib.o

OBJS3=	test-noccs.o \
	restart_counter.o

OBJS4=	dtest-noccs.o

SHAREDOBJS=	depends-noccs.o \
		fo_domain-noccs.o \
		reslist-noccs.o \
		resrules-noccs.o \
		restree-noccs.o \
		rg_locks-noccs.o \
		event_config-noccs.o

CFLAGS += -D_GNU_SOURCE -DSHAREDIR=\"${sharedir}\"
CFLAGS += -Werror -Wstrict-prototypes -Wshadow -fPIC
CFLAGS += -I${ccsincdir} -I${cmanincdir} -I${dlmincdir}
CFLAGS += `xml2-config --cflags` -I${readlineincdir}
CFLAGS += -I../../include
CFLAGS += -I${incdir}

EXTRA_CFLAGS += -DNO_CCS

LDFLAGS += -L${libdir}
LDFLAGS += -L../clulib -lclulib

CCS_LDFLAGS += -L${ccslibdir} -lccs 
CMAN_LDFLAGS += -L${cmanlibdir} -lcman
DLM_LDFLAGS += -L${dlmlibdir} -ldlm
XML2_LDFLAGS += `xml2-config --libs`
EXTRA_LDFLAGS += -lpthread -ldl -lslang
LOCAL_LDFLAGS += -llalloc
READLINE_LDFLAGS += -L${readlinelibdir} -lreadline

all: depends ${TARGET1} ${TARGET2} ${TARGET3}

${TARGET1}: ${OBJS1}
	$(CC) -o $@ $^ $(LDFLAGS) $(CCS_LDFLAGS) $(CMAN_LDFLAGS) \
			$(DLM_LDFLAGS) $(XML2_LDFLAGS) $(EXTRA_LDFLAGS)

${TARGET2}: ${OBJS2}
	$(CC) -o $@ $^ $(LDFLAGS)

#
# Our test program links against the local allocator so that
# we can see if our program is leaking memory during XML parsing, tree
# delta calculations, building/teardown of resource lists, etc.
# If it's leaking memory, the 'make check' will fail.  Also, we can
# use it to test known-good configurations for regressions.
#
# The data in the 'tests' directory is hand-crafted; so running 'gentests.sh'
# will require that the developer hand-verify the correctness of the
# resulting output prior to committing back to CVS.
#
# This is NOT meant to be an installed binary.  Rather, RPMs and/or other
# packages should run 'make check' as part of the build process.
#
${TARGET3}: ${SHAREDOBJS} ${OBJS3}
	$(CC) -o $@ $^ $(LDFLAGS) $(CCS_LDFLAGS) $(CMAN_LDFLAGS) \
			$(LOCAL_LDFLAGS) $(EXTRA_LDFLAGS) $(XML2_LDFLAGS)

${TARGET4}: ${SHAREDOBJS} ${OBJS4}
	$(CC) -o $@ $^ $(LDFLAGS) $(CCS_LDFLAGS) $(CMAN_LDFLAGS) \
			$(LOCAL_LDFLAGS) $(EXTRA_LDFLAGS) $(XML2_LDFLAGS) \
			$(READLINE_LDFLAGS)

check: rg_test
	cd tests && ./runtests.sh

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%-noccs.o: %.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c -o $@ $<

depends:
	$(MAKE) -C ../clulib all

install: all
	install -d ${sbindir}
	install -m755 ${TARGET1} ${TARGET2} ${TARGET3} ${sbindir}

uninstall:
	${UNINSTALL} ${TARGET1} ${TARGET2} ${TARGET3} ${sbindir}

clean:
	rm -f *.o ${TARGET1} ${TARGET2} ${TARGET3} ${TARGET4}
	rm -f tests/*.out*

.PHONY: all ${TARGET1} ${TARGET2} ${TARGET3} ${TARGET4}
