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
top_srcdir=..
include ${top_srcdir}/make/defines.mk
UNINSTALL=${top_srcdir}/scripts/uninstall.pl

INCLUDES+= -I. -I${incdir}
LIBS     = -L${libdir} -ldlm
CFLAGS  += -D__CLUSTER__ -D_REENTRANT
OBJS     = sm.o services.o
TARGETS  = sm.so

ifneq (${KERNEL_SRC}, )
# Use the kernel tree if patched, otherwise, look where cluster headers
#  should be installed
INCLUDES += $(shell if [ -d ${KERNEL_SRC}/include/cluster ]; then \
                echo '-I${KERNEL_SRC}/include/cluster'; else \
                echo '-I${incdir}/cluster'; fi)
else
INCLUDES += -I${incdir}/cluster
endif

all: $(TARGETS)

install: all
	mkdir -p ${libdir}/magma/plugins
	install -m 0755 $(TARGETS) ${libdir}/magma/plugins

uninstall:
	${UNINSTALL} ${TARGETS} ${libdir}/magma/plugins

sm.so: buildtest $(OBJS)
	$(CC) -o $@ $(OBJS) -shared $(CFLAGS) $(LIBS)

buildtest: $(OBJS)
	echo "int main(void) { return 0; }" > __buildtest.c
	$(CC) -c __buildtest.c
	$(CC) -o $@ __buildtest.o $(OBJS) -lxml2 $(LIBS) -lpthread
	rm -f __buildtest*

%.o: %.c
	$(CC) -c -o $@ $^ $(INCLUDES) $(CFLAGS)

clean:
	rm -f *o *~ *.so buildtest

