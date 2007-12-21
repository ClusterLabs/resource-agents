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

TARGET= fence_ack_manual

include ../../../make/defines.mk
include $(OBJDIR)/make/clean.mk

all: ${TARGET}

# Using manual override instead of the binary version.
# (this version does not require any configuration params
# in cluster.conf, but fencing must fail first)
fence_ack_manual: $(S)/fence_ack_manual.sh
	cp $< $@

install: all
	if [ ! -d ${sbindir} ]; then \
		install -d ${sbindir}; \
	fi
	install -m755 ${TARGET} ${sbindir}

uninstall:
	${UNINSTALL} ${TARGET} ${sbindir}

clean: generalclean
