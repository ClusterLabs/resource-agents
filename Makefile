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

# Order is important

all:
	cd magma && ${MAKE} all
	cd ccs && ${MAKE} all
	cd cman && ${MAKE} all
	cd dlm && ${MAKE} all
	cd fence && ${MAKE} all
	cd iddev && ${MAKE} all
	cd gfs && ${MAKE} all
	cd gnbd && ${MAKE} all
	cd gulm && ${MAKE} all
	cd magma-plugins && ${MAKE} all

copytobin:
	cd magma && ${MAKE} copytobin
	cd ccs && ${MAKE} copytobin
	cd cman && ${MAKE} copytobin
	cd dlm && ${MAKE} copytobin
	cd fence && ${MAKE} copytobin
	cd iddev && ${MAKE} copytobin
	cd gfs && ${MAKE} copytobin
	cd gnbd && ${MAKE} copytobin
	cd gulm && ${MAKE} copytobin
	cd magma-plugins && ${MAKE} copytobin

clean:
	rm -f *tar.gz
	cd magma && ${MAKE} clean
	cd ccs && ${MAKE} clean
	cd cman && ${MAKE} clean
	cd dlm && ${MAKE} clean
	cd fence && ${MAKE} clean
	cd iddev && ${MAKE} clean
	cd gfs && ${MAKE} clean
	cd gnbd && ${MAKE} clean
	cd gulm && ${MAKE} clean
	cd magma-plugins && ${MAKE} clean

distclean:
	cd magma && ${MAKE} distclean
	cd ccs && ${MAKE} distclean
	cd cman && ${MAKE} distclean
	cd dlm && ${MAKE} distclean
	cd fence && ${MAKE} distclean
	cd iddev && ${MAKE} distclean
	cd gfs && ${MAKE} distclean
	cd gnbd && ${MAKE} distclean
	cd gulm && ${MAKE} distclean
	cd magma-plugins && ${MAKE} distclean

install:
	cd magma && ${MAKE} install
	cd ccs && ${MAKE} install
	cd cman && ${MAKE} install
	cd dlm && ${MAKE} install
	cd fence && ${MAKE} install
	cd iddev && ${MAKE} install
	cd gfs && ${MAKE} install
	cd gnbd && ${MAKE} install
	cd gulm && ${MAKE} install
	cd magma-plugins && ${MAKE} install


uninstall:
	cd magma && ${MAKE} uninstall
	cd ccs && ${MAKE} uninstall
	cd cman && ${MAKE} uninstall
	cd dlm && ${MAKE} uninstall
	cd fence && ${MAKE} uninstall
	cd iddev && ${MAKE} uninstall
	cd gfs && ${MAKE} uninstall
	cd gnbd && ${MAKE} uninstall
	cd gulm && ${MAKE} uninstall
	cd magma-plugins && ${MAKE} uninstall

tarballs:
	make -s COMPONENT=magma RELEASE_FILE=magma/make/release.mk.input tarball
	make -s COMPONENT=ccs RELEASE_FILE=ccs/make/release.mk.input tarball
	make -s COMPONENT=cman RELEASE_FILE=cman/make/release.mk.input tarball
	make -s COMPONENT=cman-kernel RELEASE_FILE=cman-kernel/make/release.mk.input tarball
	make -s COMPONENT=dlm RELEASE_FILE=dlm/make/release.mk.input tarball
	make -s COMPONENT=dlm-kernel RELEASE_FILE=dlm-kernel/make/release.mk.input tarball
	make -s COMPONENT=fence RELEASE_FILE=fence/make/release.mk.input tarball
	make -s COMPONENT=iddev RELEASE_FILE=iddev/make/release.mk.input tarball
	make -s COMPONENT=gfs RELEASE_FILE=gfs/make/release.mk.input tarball
	make -s COMPONENT=gfs-kernel RELEASE_FILE=gfs-kernel/make/release.mk.input tarball
	make -s COMPONENT=gnbd RELEASE_FILE=gnbd/make/release.mk.input tarball
	make -s COMPONENT=gnbd-kernel RELEASE_FILE=gnbd-kernel/make/release.mk.input tarball
	make -s COMPONENT=gulm RELEASE_FILE=gulm/make/release.mk.input tarball
	make -s COMPONENT=magma-plugins RELEASE_FILE=magma-plugins/make/release.mk.input tarball

ifdef RELEASE_FILE
include ${RELEASE_FILE}
endif

ifneq (${RELEASE_MAJOR}, DEVEL)
tarball:
	mv ${COMPONENT} ${COMPONENT}-${RELEASE_MAJOR}.${RELEASE_MINOR}
	tar -zcvf ${COMPONENT}-${RELEASE_MAJOR}.${RELEASE_MINOR}.tar.gz \
	${COMPONENT}-${RELEASE_MAJOR}.${RELEASE_MINOR}
	mv ${COMPONENT}-${RELEASE_MAJOR}.${RELEASE_MINOR} ${COMPONENT}
else
tarball:
	echo "${COMPONENT}:: Version number not set."
endif
