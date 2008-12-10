TARGET1=utils/config-utils.sh
TARGET2=fs.sh
TARGET3=oracledb.sh

all: $(TARGET1) $(TARGET2) $(TARGET3)

include ../../../make/defines.mk
include $(OBJDIR)/make/clean.mk

RESOURCES=service.sh ip.sh nfsclient.sh nfsexport.sh \
	script.sh netfs.sh clusterfs.sh smb.sh \
	apache.sh openldap.sh samba.sh mysql.sh \
	postgres-8.sh tomcat-5.sh lvm.sh \
	vm.sh SAPInstance SAPDatabase named.sh \
	ASEHAagent.sh

METADATA=apache.metadata openldap.metadata samba.metadata \
	mysql.metadata postgres-8.metadata tomcat-5.metadata \
	named.metadata lvm.metadata default_event_script.sl \
	follow-service.sl

GENERAL_TARGETS=ocf-shellfuncs svclib_nfslock lvm_by_lv.sh lvm_by_vg.sh

UTIL_TARGETS= \
	utils/ra-skelet.sh utils/messages.sh \
	utils/httpd-parse-config.pl utils/tomcat-parse-config.pl \
	utils/member_util.sh

$(TARGET1):
	mkdir -p utils
	cat $(S)/$(TARGET1).in | sed \
		-e 's#@CONFDIR@#${CONFDIR}#g' \
		-e 's#@CONFFILE@#${CONFFILE}#g' \
	> $(TARGET1)

$(TARGET2):
	cat $(S)/$(TARGET2).in | sed \
		-e 's#@LOGDIR@#${logdir}#g' \
	> $(TARGET2)

$(TARGET3):
	cat $(S)/$(TARGET3).in | sed \
		-e 's#@LOGDIR@#${logdir}#g' \
	> $(TARGET3)

install:
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
	install -m755 $(TARGET1) ${sharedir}/utils
	install -m755 $(TARGET2) $(TARGET3) ${sharedir}

uninstall:
	${UNINSTALL} ${RESOURCES} ${GENERAL_TARGETS} \
		     ${METADATA} ${UTIL_TARGETS} \
		     $(TARGET1) $(TARGET2) $(TARGET3) \
		     ${sharedir}

clean: generalclean

check: $(RESOURCES) $(TARGET2) $(TARGET3) ra-api-1-modified.dtd
	@echo Validating resource agent meta-data
	@for f in $(RESOURCES); do \
		echo "   ./$$f "; \
		./$$f meta-data | xmllint --dtdvalid \
				  ./ra-api-1-modified.dtd --noout -; \
		if [ $$? -ne 0 ]; then exit 1; fi \
	done
