##############################################################################
###############################################################################
##
##  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
##  Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
##  
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

include ../../make/defines.mk

SUBDIRS = ${fence_agents}

all: ${SUBDIRS}

%:
	set -e && \
	for i in ${SUBDIRS}; do \
		if [ "$$i" = "xvm" ] && [ "${enable_xen}" != "1" ] && [ "$@" != "clean" ]; then \
			continue; \
		else \
			${MAKE} -C $$i $@; \
		fi; \
	done
