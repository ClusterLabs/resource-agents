MAINTAINERCLEANFILES	= Makefile.in

SUBDIRS			= utils

TARGET			= fs.sh oracledb.sh

RESOURCES		= service.sh ip.sh nfsclient.sh nfsexport.sh \
			  script.sh netfs.sh clusterfs.sh smb.sh \
			  apache.sh openldap.sh samba.sh mysql.sh \
			  postgres-8.sh tomcat-5.sh lvm.sh \
			  vm.sh SAPInstance SAPDatabase named.sh \
			  ASEHAagent.sh

METADATA		= apache.metadata openldap.metadata samba.metadata \
			  mysql.metadata postgres-8.metadata \
			  tomcat-5.metadata named.metadata lvm.metadata

EVENT_TARGETS		= default_event_script.sl follow-service.sl

HELPERS			= ocf-shellfuncs svclib_nfslock \
			  lvm_by_lv.sh lvm_by_vg.sh

DTD			= ra-api-1-modified.dtd

EXTRA_DIST		= $(TARGET:=.in) \
			  $(RESOURCES) \
			  $(METADATA) \
			  $(EVENT_TARGETS) \
			  $(HELPERS) \
			  $(DTD)

rasdir			= ${CLUSTERDATA}

ras_SCRIPTS		= $(TARGET) \
			  $(RESOURCES) \
			  $(HELPERS)

ras_DATA		= $(METADATA) \
			  $(EVENT_TARGETS)

$(TARGET): 
	cat $@.in | sed \
		-e 's#@''LOGDIR@#${LOGDIR}#g' \
	> $@

clean-local:
	rm -f $(TARGET)

ras-validation: $(RESOURCES) $(TARGET) $(DTD)
	@echo Validating resource agent meta-data
	@for f in $(RESOURCES); do \
		echo "   $(abs_srcdir)/$$f "; \
		bash $(abs_srcdir)/$$f meta-data | xmllint --dtdvalid \
			$(abs_srcdir)/$(DTD) --noout -; \
		if [ $$? -ne 0 ]; then exit 1; fi \
	done
	@for f in $(TARGET); do \
		echo "   $(abs_builddir)/$$f "; \
		bash $(abs_builddir)/$$f  meta-data | xmllint --dtdvalid \
			$(abs_srcdir)/$(DTD) --noout -; \
		if [ $$? -ne 0 ]; then exit 1; fi \
	done
