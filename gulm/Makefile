###############################################################################
###############################################################################
##
##  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
##  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################
include make/defines.mk
UNINSTALL=scripts/uninstall.pl

#source lists.
gulmd_src = \
	src/config_ccs.c \
	src/config_cmdline.c \
	src/config_main.c \
	src/core_fence.c \
	src/core_io.c \
	src/core_main.c \
	src/core_nodelists.c \
	src/core_resources.c \
	src/hash.c \
	src/hashn.c \
	src/lock_io.c \
	src/lock_main.c \
	src/lock_space.c \
	src/ltpx_io.c \
	src/ltpx_main.c \
	src/ltpx_map.c \
	src/main_main.c \
	src/myio.c \
	src/nodel.c \
	src/utils_crc.c \
	src/utils_dir.c \
	src/utils_ip.c \
	src/utils_tostr.c \
	src/utils_verb_flags.c \
	src/xdr_base.c \
	src/xdr_io.c \
	src/xdr_socket.c

lib_src = \
	lib/lg_core.c \
	lib/lg_lock.c \
	lib/lg_main.c \
	src/xdr_base.c \
	src/xdr_io.c \
	src/xdr_socket.c

tool_src = \
	src/gulm_tool.c \
	src/utils_ip.c \
	src/utils_tostr.c \
	src/utils_verb_flags.c \
	src/xdr_base.c \
	src/xdr_io.c \
	src/xdr_socket.c

# For debugging what gulm does to each lock.  This makes gulmd suck up
# memory like you cannot believe, since it is saving the last n requests on
# every lock it ever sees.  Only set this if you know how to use it.
# lock history data is in the SIGUSR1 lock dumps.
#CFLAGS+=-DLOCKHISTORY=6

INCLUDE+= -Isrc -I${incdir}

LDFLAGS+= -L${libdir}
#LDLIBS+= -lwrap -lnsl

CFLAGS+=$(INCLUDE)
# this is mostly for the lib.  Does it hurt any having it for everyone?
CFLAGS+=-D_REENTRANT -fpic
CFLAGS+=-g -O
CFLAGS+=-DRELEASE=\"$(RELEASE)\"

#######################################
# build rules

all: src/lock_gulmd src/gulm_tool lib/libgulm.so lib/libgulm.so.$(RELEASE_MAJOR)

src/lock_gulmd: $(gulmd_src:.c=.o) 
	${CC} ${CFLAGS} ${LDFLAGS} $^ ${LDLIBS} -lccs -o $@

lib/libgulm.a: $(lib_src:.c=.o)
	${AR} cr $@ $^

lib/libgulm.so.$(RELEASE_MAJOR).$(RELEASE_MINOR): lib/libgulm.a
	${LD} -shared -soname libgulm.so.$(RELEASE_MAJOR) -o $@ $^ -lc

lib/libgulm.so.$(RELEASE_MAJOR): lib/libgulm.so.$(RELEASE_MAJOR).$(RELEASE_MINOR)
	ln -snf libgulm.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) $@

lib/libgulm.so: lib/libgulm.so.$(RELEASE_MAJOR).$(RELEASE_MINOR)
	ln -snf libgulm.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) $@

src/gulm_tool: $(tool_src:.c=.o)
	${CC} ${CFLAGS} ${LDFLAGS} $^ ${LDLIBS} -o $@

.c.o:
	${CC} ${CFLAGS} ${LDFLAGS} -c -o $@ $<

clean:
	find ./ -name '*.o' -print | xargs rm -f dummy
	rm -f src/lock_gulmd src/gulm_tool lib/libgulm.a lib/libgulm.so*

distclean: clean
	rm -f make/defines.mk cscope.files cscope.out

install: all
	install -d ${sbindir}
	install -m755 src/lock_gulmd src/gulm_tool ${sbindir}
	install -d ${mandir}/man5
	install man/lock_gulmd.5 ${mandir}/man5
	install -d ${mandir}/man8
	install man/lock_gulmd.8 man/gulm_tool.8 ${mandir}/man8
	install -d ${libdir}
	install -m644 lib/libgulm.a ${libdir}
	cd ${libdir}; ln -snf libgulm.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) libgulm.so.$(RELEASE_MAJOR); cd - 
	cd ${libdir}; ln -snf libgulm.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) libgulm.so; cd - 
	install -m644 lib/libgulm.so ${libdir}
	install -d ${incdir}
	install -m644 lib/libgulm.h ${incdir}
	cd init.d && ${MAKE} install


uninstall:
	${UNINSTALL} lock_gulmd gulm_tool ${sbindir}
	${UNINSTALL} lock_gulmd.5 ${mandir}/man5
	${UNINSTALL} lock_gulmd.8 gulm_tool.8 ${mandir}/man8
	${UNINSTALL} libgulm.a ${libdir}
	${UNINSTALL} libgulm.h ${incdir}
	cd init.d && ${MAKE} uninstall
	rm -f /lib/libgulm.*

cscope cscope.out: $(gulmd_src) $(tool_src) $(lib_src)
	@find ./ -name '*.[chlyCGHL]' -type f -print > cscope.files
	@cscope -b

dep depends:
	${CC} ${CFLAGS} ${LDFLAGS} -MM $(tool_src) $(lib_src) $(gulmd_src) > depends

ifeq (depends, $(wildcard depends))
include depends
endif
