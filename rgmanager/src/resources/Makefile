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

top_srcdir=../..
# Currently no un-install dir.
#UNINSTALL=${top_srcdir}/scripts/uninstall.pl

include ${top_srcdir}/make/defines.mk
INCLUDE += -I $(top_srcdir)/include

TARGETS=fs.sh group.sh ip.sh nfsclient.sh nfsexport.sh \
	script.sh
all:

install:
	echo ${sharedir}
	echo ${sbindir}
	install -d ${sharedir}
	install $(TARGETS) ${sharedir}

clean:
