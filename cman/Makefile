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

include ../make/defines.mk

SUBDIRS=lib cman_tool daemon qdisk init.d man

all: ${SUBDIRS}

%:
	set -e && \
	for i in ${SUBDIRS}; do \
		${MAKE} -C $$i $@; \
	done
