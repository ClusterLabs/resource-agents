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

SOURCE= ccs_tool.pl
TARGET= ccs_tool

top_srcdir=..
UNINSTALL = ${top_srcdir}/scripts/uninstall.pl
include ${top_srcdir}/make/defines.mk

all: $(TARGET)

ccs_tool: $(SOURCE)
	cp $(SOURCE) $(TARGET)
	chmod +x $(TARGET)

clean:
	rm -f $(TARGET)

install: all
	install -d ${sbindir}
	install $(TARGET) ${sbindir}

uninstall:
	${UNINSTALL} $(TARGET) ${sbindir}