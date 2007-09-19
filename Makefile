###############################################################################
###############################################################################
##  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
##  
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

include make/defines.mk

KERNEL=gnbd-kernel gfs-kernel
USERLAND=cman-lib ccs cman dlm group fence gfs gfs2 gnbd rgmanager

MODULES=${KERNEL} ${USERLAND}

KSUBDIRS=gnbd-kernel/src gfs-kernel/src/gfs
SUBDIRS=ccs cman dlm group fence gfs gfs2 gnbd rgmanager

all: build

build: kernel-modules userland

# scripts details
scripts:
	chmod 755 ${BUILDDIR}/scripts/*.pl
	chmod 755 ${BUILDDIR}/scripts/define2var

# kernel stuff

kernel-modules: ${KERNEL}

gnbd-kernel:
	${MAKE} -C gnbd-kernel/src all

gfs-kernel:
	${MAKE} -C gfs-kernel/src/gfs

# userland stuff
# make all target can't be folded in a for loop otherwise make -j breaks
# because we can't express dependencies.

userland: scripts ${USERLAND}

cman-lib:
	${MAKE} -C cman/lib all

ccs: cman-lib
	${MAKE} -C ccs all

cman: ccs
	${MAKE} -C cman all

dlm:
	${MAKE} -C dlm all

group: ccs dlm
	${MAKE} -C group all

fence: group dlm
	${MAKE} -C fence all

gfs:
	${MAKE} -C gfs all

gfs2:
	${MAKE} -C gfs2 all

gnbd: cman-lib
	${MAKE} -C gnbd all

rgmanager: ccs dlm
	${MAKE} -C rgmanager all

# end of build

clean:
	set -e && for i in ${KSUBDIRS} ${SUBDIRS}; do ${MAKE} -C $$i $@; done

distclean: clean
	rm -f make/defines.mk
	rm -f *tar.gz
	rm -rf build

kernel-install: kernel-modules
	set -e && for i in ${KSUBDIRS}; do ${MAKE} -C $$i install; done

userland-install: userland
	set -e && for i in ${SUBDIRS}; do ${MAKE} -C $$i install; done

install: kernel-install userland-install

uninstall:
	set -e && for i in ${KSUBDIRS} ${SUBDIRS}; do ${MAKE} -C $$i $@; done

.PHONY: scripts ${MODULES}
