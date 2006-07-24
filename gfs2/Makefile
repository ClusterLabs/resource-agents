###############################################################################
###############################################################################
##
##  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
##  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

all: tag_libgfs2 tag_convert tag_edit tag_fsck tag_mkfs tag_mount tag_tool

tag_libgfs2:
	${MAKE} -C libgfs2 all
tag_convert:
	${MAKE} -C convert all
tag_edit:
	${MAKE} -C edit all
tag_fsck:
	${MAKE} -C fsck all
tag_mkfs:
	${MAKE} -C mkfs all
tag_mount:
	${MAKE} -C mount all
tag_quota:
	${MAKE} -C quota all
tag_tool:
	${MAKE} -C tool all

copytobin:
	${MAKE} -C convert copytobin
	${MAKE} -C edit copytobin
	${MAKE} -C fsck copytobin
	${MAKE} -C mkfs copytobin
	${MAKE} -C quota copytobin
	${MAKE} -C tool copytobin

clean:
	${MAKE} -C libgfs2 clean
	${MAKE} -C convert clean
	${MAKE} -C edit clean
	${MAKE} -C fsck clean
	${MAKE} -C mkfs clean
	${MAKE} -C mount clean
	${MAKE} -C quota clean
	${MAKE} -C tool clean
	${MAKE} -C bin clean

distclean: clean
	rm -f make/defines.mk

install:
	${MAKE} -C mount install
	${MAKE} -C bin install
	${MAKE} -C man install
	${MAKE} -C init.d install

deinstall uninstall:
	${MAKE} -C mount uninstall
	${MAKE} -C bin uninstall
	${MAKE} -C man uninstall
	${MAKE} -C init.d uninstall
