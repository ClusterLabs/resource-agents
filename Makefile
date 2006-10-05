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
#	${MAKE} -C gnbd-kernel all
	${MAKE} -C cman/lib all
	${MAKE} -C ccs all
	${MAKE} -C cman all
	${MAKE} -C group all
	${MAKE} -C dlm all
	${MAKE} -C fence all
#	${MAKE} -C gfs-kernel all
	${MAKE} -C gfs all
	${MAKE} -C gfs2 all
	${MAKE} -C gnbd all
	${MAKE} -C rgmanager all
#	${MAKE} -C cmirror all

clean:
	rm -f *tar.gz
	rm -rf build
#	${MAKE} -C gfs-kernel clean
#	${MAKE} -C gnbd-kernel clean
	${MAKE} -C ccs clean
	${MAKE} -C cman clean
	${MAKE} -C group clean
	${MAKE} -C dlm clean
	${MAKE} -C fence clean
	${MAKE} -C gfs clean
	${MAKE} -C gfs2 clean
	${MAKE} -C gnbd clean
	${MAKE} -C rgmanager clean
#	${MAKE} -C cmirror clean

distclean:
#	${MAKE} -C gfs-kernel distclean
#	${MAKE} -C gnbd-kernel distclean
	${MAKE} -C ccs distclean
	${MAKE} -C cman distclean
	${MAKE} -C group distclean
	${MAKE} -C dlm distclean
	${MAKE} -C fence distclean
	${MAKE} -C gfs distclean
	${MAKE} -C gfs2 distclean
	${MAKE} -C gnbd distclean
	${MAKE} -C rgmanager distclean
#	${MAKE} -C cmirror distclean

install: all
#	${MAKE} -C gfs-kernel install
#	${MAKE} -C gnbd-kernel install
	${MAKE} -C ccs install
	${MAKE} -C cman install
	${MAKE} -C group install
	${MAKE} -C dlm install
	${MAKE} -C fence install
	${MAKE} -C gfs install
	${MAKE} -C gfs2 install
	${MAKE} -C gnbd install
	${MAKE} -C rgmanager install
#	${MAKE} -C cmirror install

uninstall:
#	${MAKE} -C gfs-kernel uninstall
#	${MAKE} -C gnbd-kernel uninstall
	${MAKE} -C ccs uninstall
	${MAKE} -C cman uninstall
	${MAKE} -C group uninstall
	${MAKE} -C dlm uninstall
	${MAKE} -C fence uninstall
	${MAKE} -C gfs uninstall
	${MAKE} -C gfs2 uninstall
	${MAKE} -C gnbd uninstall
	${MAKE} -C rgmanager uninstall
#	${MAKE} -C cmirror uninstall

latest_tags:
#	${LATEST_TAG} gfs-kernel
#	${LATEST_TAG} gnbd-kernel
	${LATEST_TAG} ccs
	${LATEST_TAG} cman
	${LATEST_TAG} dlm
	${LATEST_TAG} fence
	${LATEST_TAG} gfs
	${LATEST_TAG} gfs2
	${LATEST_TAG} gnbd
	${LATEST_TAG} rgmanager
	echo "Beware, your directories are now in sync with their last tag." > TAG
#	${LATEST_TAG} cmirror

setrelease:
	for i in `ls */make/release.mk.input`; do ${EDITOR} $$i; done

.PHONY: srpms

srpms:
	$(BUILD_SRPMS)

tarballs: TAG
#	make -s COMPONENT=gfs-kernel RELEASE_FILE=gfs-kernel/make/release.mk.input tarball
#	make -s COMPONENT=gnbd-kernel RELEASE_FILE=gnbd-kernel/make/release.mk.input tarball
	make -s COMPONENT=ccs RELEASE_FILE=ccs/make/release.mk.input tarball
	make -s COMPONENT=cman RELEASE_FILE=cman/make/release.mk.input tarball
	make -s COMPONENT=dlm RELEASE_FILE=dlm/make/release.mk.input tarball
	make -s COMPONENT=fence RELEASE_FILE=fence/make/release.mk.input tarball
	make -s COMPONENT=gfs RELEASE_FILE=gfs/make/release.mk.input tarball
	make -s COMPONENT=gfs2 RELEASE_FILE=gfs2/make/release.mk.input tarball
	make -s COMPONENT=gnbd RELEASE_FILE=gnbd/make/release.mk.input tarball
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
