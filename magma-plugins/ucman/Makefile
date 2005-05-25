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

INCLUDES+= -I. -I${magmaincdir} -I${cmanuincdir} -I${incdir} -I../../dlm/lib/ -I../../cman/lib/
LIBS    += -L${libdir} -ldlm_lt -L${cmanuincdir} -lcman
CFLAGS  += -D_CLUSTER_
OBJS     = ucman.o
TARGETS  = magma_ucman.so

LDFLAGS+= -L../../dlm/lib

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
	install -d ${libdir}/magma
	install -m 0755 $(TARGETS) ${libdir}/magma

uninstall:
	${UNINSTALL} ${TARGETS} ${libdir}/magma

magma_ucman.so: buildtest $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -shared $(CFLAGS) $(LIBS)

buildtest: $(OBJS)
	echo "int main(void) { return 0; }" > _buildtest.c
	$(CC) -c _buildtest.c
	$(CC) -o $@ _buildtest.o $(OBJS) $(LIBS) $(LDFLAGS)
	rm -f _buildtest*

%.o: %.c
	$(CC) -c -o $@ $^ $(INCLUDES) -fPIC $(CFLAGS)

clean:
	rm -f *o *~ *.so buildtest

