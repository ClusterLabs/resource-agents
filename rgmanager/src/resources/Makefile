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

UNINSTALL=${top_srcdir}/scripts/uninstall.pl

include ${top_srcdir}/make/defines.mk
INCLUDE += -I $(top_srcdir)/include

TARGETS=fs.sh resourcegroup.sh ip.sh nfsclient.sh nfsexport.sh \
	script.sh netfs.sh clusterfs.sh # oracleas
all:

install: all
	echo ${sharedir}
	echo ${sbindir}
	install -d ${sharedir}
	install $(TARGETS) ${sharedir}

uninstall:
	${UNINSTALL} ${TARGETS} ${sharedir}

clean:

check: $(TARGETS) ra-api-1-modified.dtd
	@echo Validating resource agent meta-data
	@for f in $(TARGETS); do \
		echo "   ./$$f "; \
		./$$f meta-data | xmllint --dtdvalid \
				  ./ra-api-1-modified.dtd --noout -; \
		if [ $$? -ne 0 ]; then exit 1; fi \
	done
