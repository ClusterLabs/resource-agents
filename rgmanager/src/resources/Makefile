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

include ../../../make/defines.mk

RESOURCES=fs.sh service.sh ip.sh nfsclient.sh nfsexport.sh \
	script.sh netfs.sh clusterfs.sh smb.sh \
	apache.sh openldap.sh samba.sh mysql.sh \
	postgres-8.sh tomcat-5.sh lvm.sh vm.sh \
	SAPInstance SAPDatabase named.sh

METADATA=apache.metadata openldap.metadata samba.metadata \
	mysql.metadata postgres-8.metadata tomcat-5.metadata \
	named.metadata

TARGETS=ocf-shellfuncs svclib_nfslock default_event_script.sl

UTIL_TARGETS= \
	utils/config-utils.sh utils/ra-skelet.sh utils/messages.sh \
	utils/httpd-parse-config.pl utils/tomcat-parse-config.pl \
	utils/member_util.sh

all:

install: all
	echo ${sharedir}
	echo ${sbindir}
	install -d ${sharedir}
	install -d ${sharedir}/utils
	install $(RESOURCES) ${sharedir}
	install -m 644 $(TARGETS) ${sharedir}
	install $(UTIL_TARGETS) ${sharedir}/utils
	install -m 644 $(METADATA) ${sharedir}

uninstall:
	${UNINSTALL} ${TARGETS} ${METADATA} ${UTIL_TARGETS} ${sharedir}

clean:

check: $(RESOURCES) ra-api-1-modified.dtd
	@echo Validating resource agent meta-data
	@for f in $(RESOURCES); do \
		echo "   ./$$f "; \
		./$$f meta-data | xmllint --dtdvalid \
				  ./ra-api-1-modified.dtd --noout -; \
		if [ $$? -ne 0 ]; then exit 1; fi \
	done
