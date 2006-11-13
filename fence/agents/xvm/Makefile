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

top_srcdir=../..
include ${top_srcdir}/make/defines.mk

TARGETS=fence_xvm fence_xvmd

fence_xvm_SOURCE = fence_xvm.c mcast.c ip_lookup.c simple_auth.c tcp.c \
		   options.c debug.c
fence_xvmd_SOURCE= fence_xvmd.c mcast.c simple_auth.c tcp.c virt.c \
		   options.c options-ccs.c vm_states.c debug.c


INCLUDE=-I${top_srcdir}/include -I${top_srcdir}/config \
	-I/usr/include/openais -I/usr/include/libvirt \
	-I/usr/include/nss3 -I/usr/include/nspr4 \
	-I../../../cman/lib -I../../../ccs/lib

CFLAGS+=-DFENCE_RELEASE_NAME=\"${RELEASE}\" \
	-Wall -Werror -Wstrict-prototypes -Wshadow -ggdb -D_GNU_SOURCE

LIBS+=-L../../../cman/lib -L../../../ccs/lib -L${libdir}/openais \
      -L../../../dlm/lib -lnss3

all: ${TARGETS}

fence_xvm: ${fence_xvm_SOURCE:.c=.o}
	gcc -o $@ $^ $(LIBS)

fence_xvmd: ${fence_xvmd_SOURCE:.c=.o}
	gcc -o $@ $^ $(LIBS) -lvirt -lccs -lcman -lSaCkpt

%.o: %.c
	gcc $(CFLAGS) -c -o $@ $^ $(INCLUDES)

clean:
	rm -f $(TARGETS) *~ *.o

install: all
	if [ ! -d ${sbindir} ]; then \
		install -d ${sbindir}; \
	fi
	install -m755 ${TARGETS} ${sbindir}


