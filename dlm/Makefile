###############################################################################
###############################################################################
##
##  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

SUBDIRS=lib tool

%:
	set -e && \
	for i in ${SUBDIRS}; do \
		${MAKE} -C $$i $@; \
	done
