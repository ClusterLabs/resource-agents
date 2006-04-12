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
BUILD_SRPMS=scripts/build_srpms.pl
BUILDDIR = $(shell pwd)/build
MAKELINE =  sbindir=${BUILDDIR}/sbin libdir=${BUILDDIR}/lib mandir=${BUILDDIR}/man incdir=${BUILDDIR}/incdir module_dir=${BUILDDIR}/module sharedir=${BUILDDIR} slibdir=${BUILDDIR}/slib DESTDIR=${BUILDDIR}


all:
	cd gnbd-kernel && ${MAKE}
	cd ccs && ${MAKE}
	cd cman && ${MAKE}
	cd group && ${MAKE}
	cd dlm && ${MAKE}
	cd fence && ${MAKE}
	cd gulm && ${MAKE}
	cd gfs-kernel && ${MAKE}
	cd gfs && ${MAKE}
	cd gfs2 && ${MAKE}
	cd gnbd && ${MAKE}
	cd rgmanager && ${MAKE}
#	cd cmirror && ${MAKE}

copytobin:
	cd gfs-kernel && ${MAKE} copytobin
	cd gnbd-kernel && ${MAKE} copytobin
	cd ccs && ${MAKE} copytobin
	cd cman && ${MAKE} copytobin
	cd dlm && ${MAKE} copytobin
	cd fence && ${MAKE} copytobin
	cd gfs && ${MAKE} copytobin
	cd gfs2 && ${MAKE} copytobin
	cd gnbd && ${MAKE} copytobin
	cd gulm && ${MAKE} copytobin
	cd rgmanager && ${MAKE} copytobin
#	cd cmirror && ${MAKE} copytobin

clean:
	rm -f *tar.gz
	rm -rf build
	cd gfs-kernel && ${MAKE} clean
	cd gnbd-kernel && ${MAKE} clean
	cd ccs && ${MAKE} clean
	cd cman && ${MAKE} clean
	cd group && ${MAKE} clean
	cd dlm && ${MAKE} clean
	cd fence && ${MAKE} clean
	cd gfs && ${MAKE} clean
	cd gfs2 && ${MAKE} clean
	cd gnbd && ${MAKE} clean
	cd gulm && ${MAKE} clean
	cd rgmanager && ${MAKE} clean
#	cd cmirror && ${MAKE} clean

distclean:
	cd gfs-kernel && ${MAKE} distclean
	cd gnbd-kernel && ${MAKE} distclean
	cd ccs && ${MAKE} distclean
	cd cman && ${MAKE} distclean
	cd dlm && ${MAKE} distclean
	cd fence && ${MAKE} distclean
	cd gfs && ${MAKE} distclean
	cd gfs2 && ${MAKE} distclean
	cd gnbd && ${MAKE} distclean
	cd gulm && ${MAKE} distclean
	cd rgmanager && ${MAKE} distclean
#	cd cmirror && ${MAKE} distclean

install:
	cd gfs-kernel && ${MAKE} install
	cd gnbd-kernel && ${MAKE} install
	cd ccs && ${MAKE} install
	cd cman && ${MAKE} install
	cd group && ${MAKE} install
	cd dlm && ${MAKE} install
	cd fence && ${MAKE} install
	cd gfs && ${MAKE} install
	cd gfs2 && ${MAKE} install
	cd gnbd && ${MAKE} install
	cd gulm && ${MAKE} install
	cd rgmanager && ${MAKE} install
#	cd cmirror && ${MAKE} install

uninstall:
	cd gfs-kernel && ${MAKE} uninstall
	cd gnbd-kernel && ${MAKE} uninstall
	cd ccs && ${MAKE} uninstall
	cd cman && ${MAKE} uninstall
	cd dlm && ${MAKE} uninstall
	cd fence && ${MAKE} uninstall
	cd gfs && ${MAKE} uninstall
	cd gfs2 && ${MAKE} uninstall
	cd gnbd && ${MAKE} uninstall
	cd gulm && ${MAKE} uninstall
	cd rgmanager && ${MAKE} uninstall
#	cd cmirror && ${MAKE} uninstall

latest_tags:
	${LATEST_TAG} gfs-kernel
	${LATEST_TAG} gnbd-kernel
	${LATEST_TAG} ccs
	${LATEST_TAG} cman
	${LATEST_TAG} dlm
	${LATEST_TAG} fence
	${LATEST_TAG} gfs
	${LATEST_TAG} gfs2
	${LATEST_TAG} gnbd
	${LATEST_TAG} gulm
	${LATEST_TAG} rgmanager
	echo "Beware, your directories are now in sync with their last tag." > TAG
#	${LATEST_TAG} cmirror

setrelease:
	for i in `ls */make/release.mk.input`; do ${EDITOR} $$i; done

.PHONY: srpms

srpms:
	$(BUILD_SRPMS)

tarballs: TAG
	make -s COMPONENT=gfs-kernel RELEASE_FILE=gfs-kernel/make/release.mk.input tarball
	make -s COMPONENT=gnbd-kernel RELEASE_FILE=gnbd-kernel/make/release.mk.input tarball
	make -s COMPONENT=ccs RELEASE_FILE=ccs/make/release.mk.input tarball
	make -s COMPONENT=cman RELEASE_FILE=cman/make/release.mk.input tarball
	make -s COMPONENT=dlm RELEASE_FILE=dlm/make/release.mk.input tarball
	make -s COMPONENT=fence RELEASE_FILE=fence/make/release.mk.input tarball
	make -s COMPONENT=gfs RELEASE_FILE=gfs/make/release.mk.input tarball
	make -s COMPONENT=gfs2 RELEASE_FILE=gfs2/make/release.mk.input tarball
	make -s COMPONENT=gnbd RELEASE_FILE=gnbd/make/release.mk.input tarball
	make -s COMPONENT=gulm RELEASE_FILE=gulm/make/release.mk.input tarball
	make -s COMPONENT=rgmanager RELEASE_FILE=rgmanager/make/release.mk.input tarball
#	make -s COMPONENT=cmirror RELEASE_FILE=cmirror/make/release.mk.input tarball

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
