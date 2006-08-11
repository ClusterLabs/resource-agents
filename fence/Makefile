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
all:
	${MAKE} -C agents all
	${MAKE} -C fence_node all
	${MAKE} -C fence_tool all
	${MAKE} -C fenced all

clean:
	${MAKE} -C agents clean
	${MAKE} -C fence_node clean
	${MAKE} -C fence_tool clean
	${MAKE} -C fenced clean

distclean: clean
	rm -f make/defines.mk

install: all
	if [ ! -d ${sbindir} ]; then \
		install -d ${sbindir}; \
	fi
	${MAKE} -C agents install
	${MAKE} -C fence_node install
	${MAKE} -C fence_tool install
	${MAKE} -C fenced install
	${MAKE} -C man install

uninstall:
	${MAKE} -C agents uninstall
	${MAKE} -C fence_node uninstall
	${MAKE} -C fence_tool uninstall
	${MAKE} -C fenced uninstall
	${MAKE} -C man uninstall
