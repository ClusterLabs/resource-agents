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
LATEST_TAG=scripts/latest_tag.pl
BUILDDIR = $(shell pwd)/build
MAKELINE =  sbindir=${BUILDDIR}/sbin libdir=${BUILDDIR}/lib mandir=${BUILDDIR}/man incdir=${BUILDDIR}/incdir module_dir=${BUILDDIR}/module


all:
	cd cman-kernel && ${MAKE} install ${MAKELINE} 
	cd dlm-kernel && ${MAKE} install ${MAKELINE}
	cd gfs-kernel && ${MAKE} install ${MAKELINE}
	cd gnbd-kernel && ${MAKE} install ${MAKELINE}
	cd magma && ${MAKE} install ${MAKELINE}
	cd ccs && ${MAKE} install ${MAKELINE}
	cd cman && ${MAKE} install ${MAKELINE}
	cd dlm && ${MAKE} install ${MAKELINE}
	cd fence && ${MAKE} install ${MAKELINE}
	cd iddev && ${MAKE} install ${MAKELINE}
	cd gfs && ${MAKE} install ${MAKELINE}
	cd gnbd && ${MAKE} install ${MAKELINE}
	cd gulm && ${MAKE} install ${MAKELINE}
	cd magma-plugins && ${MAKE} install ${MAKELINE}

copytobin:
	cd cman-kernel && ${MAKE} copytobin
	cd dlm-kernel && ${MAKE} copytobin
	cd gfs-kernel && ${MAKE} copytobin
	cd gnbd-kernel && ${MAKE} copytobin
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
	rm -rf build
	cd cman-kernel && ${MAKE} clean
	cd dlm-kernel && ${MAKE} clean
	cd gfs-kernel && ${MAKE} clean
	cd gnbd-kernel && ${MAKE} clean
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
	cd cman-kernel && ${MAKE} distclean
	cd dlm-kernel && ${MAKE} distclean
	cd gfs-kernel && ${MAKE} distclean
	cd gnbd-kernel && ${MAKE} distclean
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
	cd cman-kernel && ${MAKE} install
	cd dlm-kernel && ${MAKE} install
	cd gfs-kernel && ${MAKE} install
	cd gnbd-kernel && ${MAKE} install
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
	cd cman-kernel && ${MAKE} uninstall
	cd dlm-kernel && ${MAKE} uninstall
	cd gfs-kernel && ${MAKE} uninstall
	cd gnbd-kernel && ${MAKE} uninstall
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

latest_tags:
	${LATEST_TAG} cman-kernel
	${LATEST_TAG} dlm-kernel
	${LATEST_TAG} gfs-kernel
	${LATEST_TAG} gnbd-kernel
	${LATEST_TAG} magma
	${LATEST_TAG} ccs
	${LATEST_TAG} cman
	${LATEST_TAG} dlm
	${LATEST_TAG} fence
	${LATEST_TAG} iddev
	${LATEST_TAG} gfs
	${LATEST_TAG} gnbd
	${LATEST_TAG} gulm
	${LATEST_TAG} magma-plugins
	echo "Beware, your directories are now in sync with their last tag." > TAG

setrelease:
	for i in `ls */make/release.mk.input`; do ${EDITOR} $$i; done

tarballs: TAG
	make -s COMPONENT=cman-kernel RELEASE_FILE=cman-kernel/make/release.mk.input tarball
	make -s COMPONENT=dlm-kernel RELEASE_FILE=dlm-kernel/make/release.mk.input tarball
	make -s COMPONENT=gfs-kernel RELEASE_FILE=gfs-kernel/make/release.mk.input tarball
	make -s COMPONENT=gnbd-kernel RELEASE_FILE=gnbd-kernel/make/release.mk.input tarball
	make -s COMPONENT=magma RELEASE_FILE=magma/make/release.mk.input tarball
	make -s COMPONENT=ccs RELEASE_FILE=ccs/make/release.mk.input tarball
	make -s COMPONENT=cman RELEASE_FILE=cman/make/release.mk.input tarball
	make -s COMPONENT=dlm RELEASE_FILE=dlm/make/release.mk.input tarball
	make -s COMPONENT=fence RELEASE_FILE=fence/make/release.mk.input tarball
	make -s COMPONENT=iddev RELEASE_FILE=iddev/make/release.mk.input tarball
	make -s COMPONENT=gfs RELEASE_FILE=gfs/make/release.mk.input tarball
	make -s COMPONENT=gnbd RELEASE_FILE=gnbd/make/release.mk.input tarball
	make -s COMPONENT=gulm RELEASE_FILE=gulm/make/release.mk.input tarball
	make -s COMPONENT=magma-plugins RELEASE_FILE=magma-plugins/make/release.mk.input tarball

ifdef RELEASE_FILE
include ${RELEASE_FILE}
endif

ifneq (${RELEASE_MAJOR}, DEVEL)
tarball:
	cp -r ${COMPONENT} ${COMPONENT}-${RELEASE_MAJOR}.${RELEASE_MINOR}
	rm -rf `find ${COMPONENT}-${RELEASE_MAJOR}.${RELEASE_MINOR} -name CVS`;
	tar -zcvf ${COMPONENT}-${RELEASE_MAJOR}.${RELEASE_MINOR}.tar.gz \
	${COMPONENT}-${RELEASE_MAJOR}.${RELEASE_MINOR}
	rm -rf ${COMPONENT}-${RELEASE_MAJOR}.${RELEASE_MINOR}
else
tarball:
	echo "${COMPONENT}:: Version number not set."
endif
