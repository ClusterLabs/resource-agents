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
UNINSTALL = ${top_srcdir}/scripts/uninstall.pl

TARGETS=libmagma.a \
	libmagma_nt.a \
	libmagmamsg.a \
	libmagma.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) \
	libmagma.so.$(RELEASE_MAJOR) \
	libmagma_nt.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) \
	libmagma_nt.so.$(RELEASE_MAJOR) \
	libmagmamsg.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) \
	libmagmamsg.so.$(RELEASE_MAJOR) \
	libmagma.so \
	libmagma_nt.so \
	libmagmamsg.so

INCLUDE=-I.
CFLAGS+=-g -Werror -Wstrict-prototypes -Wshadow \
	-fPIC -DPLUGINDIR=\"${libdir}/magma/plugins\" \
	-D_GNU_SOURCE

all: $(TARGETS)

clean:
	rm -f *o *~ *.a $(TARGETS)

install: all
	install -d $(libdir)

	#
	# Install high level magma libraries
	#
	install -m 0644 libmagma.a $(libdir)
	install -m 0644 libmagma.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) $(libdir)
	cd $(libdir); ln -snf libmagma.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) libmagma.so.$(RELEASE_MAJOR); cd -
	cd $(libdir); ln -snf libmagma.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) libmagma.so; cd -

	#
	# Install low-overhead, non-pthread magma libraries
	#
	install -m 0644 libmagma_nt.a $(libdir)
	install -m 0644 libmagma_nt.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) $(libdir)
	cd $(libdir); ln -snf libmagma_nt.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) libmagma_nt.so.$(RELEASE_MAJOR); cd -
	cd $(libdir); ln -snf libmagma_nt.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) libmagma_nt.so; cd -

	#
	# Install high level TCP messaging libraries
	#
	install -m 0644 libmagmamsg.a $(libdir)
	install -m 0644 libmagmamsg.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) $(libdir)
	cd $(libdir); ln -snf libmagmamsg.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) libmagmamsg.so.$(RELEASE_MAJOR); cd -
	cd $(libdir); ln -snf libmagmamsg.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) libmagmamsg.so; cd -

	#
	# Install headers for magma, magma_nt, and magmamsg
	#
	install -d ${incdir}
	install -m 0644 magma.h $(incdir)
	install -m 0644 magmamsg.h $(incdir)
	install -m 0644 magma-build.h $(incdir)	

uninstall:
	${UNINSTALL} ${TARGETS} ${libdir}
	${UNINSTALL} magma.h magmamsg.h magma-build.h ${incdir}

libmagma.so: libmagma.so.$(RELEASE_MAJOR).$(RELEASE_MINOR)
	ln -snf $^ $@

libmagma_nt.so: libmagma_nt.so.$(RELEASE_MAJOR).$(RELEASE_MINOR)
	ln -snf $^ $@

libmagmamsg.so: libmagmamsg.so.$(RELEASE_MAJOR).$(RELEASE_MINOR)
	ln -snf $^ $@

libmagma.so.$(RELEASE_MAJOR): libmagma.so.$(RELEASE_MAJOR).$(RELEASE_MINOR)
	ln -snf $^ $@

libmagma_nt.so.$(RELEASE_MAJOR): libmagma_nt.so.$(RELEASE_MAJOR).$(RELEASE_MINOR)
	ln -snf $^ $@

libmagmamsg.so.$(RELEASE_MAJOR): libmagmamsg.so.$(RELEASE_MAJOR).$(RELEASE_MINOR)
	ln -snf $^ $@

libmagma.so.$(RELEASE_MAJOR).$(RELEASE_MINOR): global.o plugin.o \
			localinfo.o ip_lookup.o memberlist.o clist.o
	${LD} -shared -soname libmagma.so.$(RELEASE_MAJOR) -o $@ $^ -lc

libmagma_nt.so.$(RELEASE_MAJOR).$(RELEASE_MINOR): plugin.o localinfo.o \
			ip_lookup.o memberlist.o
	${LD} -shared -soname libmagma_nt.so.$(RELEASE_MAJOR) -o $@ $^ -lc

libmagmamsg.so.$(RELEASE_MAJOR).$(RELEASE_MINOR): message.o fdops.o
	${LD} -shared -soname libmagmamsg.so.$(RELEASE_MAJOR) -o $@ $^ -lc

libmagma.a: global.o plugin.o localinfo.o ip_lookup.o \
		        memberlist.o clist.o
	${AR} cr $@ $^

libmagma_nt.a: plugin.o localinfo.o ip_lookup.o memberlist.o
	${AR} cr $@ $^

libmagmamsg.a: message.o fdops.o
	${AR} cr $@ $^

%.o: %.c
	$(CC) -c -o $@ $^ $(INCLUDE) $(CFLAGS) -D_CLUSTER_
