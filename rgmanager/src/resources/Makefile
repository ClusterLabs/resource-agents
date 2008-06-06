TARGET=	utils/config-utils.sh

all: $(TARGET)

include ../../../make/defines.mk
include $(OBJDIR)/make/clean.mk

RESOURCES=fs.sh service.sh ip.sh nfsclient.sh nfsexport.sh \
	script.sh netfs.sh clusterfs.sh smb.sh \
	apache.sh openldap.sh samba.sh mysql.sh \
	postgres-8.sh tomcat-5.sh lvm.sh lvm_by_lv.sh lvm_by_vg.sh \
	vm.sh SAPInstance SAPDatabase named.sh \
	oracledb.sh ASEHAagent.sh

METADATA=apache.metadata openldap.metadata samba.metadata \
	mysql.metadata postgres-8.metadata tomcat-5.metadata \
	named.metadata lvm.metadata default_event_script.sl

GENERAL_TARGETS=ocf-shellfuncs svclib_nfslock

UTIL_TARGETS= \
	utils/ra-skelet.sh utils/messages.sh \
	utils/httpd-parse-config.pl utils/tomcat-parse-config.pl \
	utils/member_util.sh

$(TARGET):
	mkdir -p utils
	cat $(S)/$(TARGET).in | sed \
		-e 's#@CONFDIR@#${CONFDIR}#g' \
		-e 's#@CONFFILE@#${CONFFILE}#g' \
	> $(TARGET)

install: all
	install -d ${sharedir}/utils
	for i in $(RESOURCES); do \
	 install -m755 $(S)/$$i ${sharedir}; \
	done
	for i in $(METADATA); do \
	 install -m644 $(S)/$$i ${sharedir}; \
	done
	for i in $(GENERAL_TARGETS); do \
	 install -m755 $(S)/$$i ${sharedir}; \
	done
	for i in $(UTIL_TARGETS); do \
	 install -m755 $(S)/$$i ${sharedir}/utils; \
	done
	install -m755 $(TARGET) ${sharedir}/utils

uninstall:
	${UNINSTALL} ${RESOURCES} ${GENERAL_TARGETS} \
		     ${METADATA} ${UTIL_TARGETS} \
		     $(TARGET) \
		     ${sharedir}

clean: generalclean

check: $(RESOURCES) ra-api-1-modified.dtd
	@echo Validating resource agent meta-data
	@for f in $(RESOURCES); do \
		echo "   ./$$f "; \
		./$$f meta-data | xmllint --dtdvalid \
				  ./ra-api-1-modified.dtd --noout -; \
		if [ $$? -ne 0 ]; then exit 1; fi \
	done
