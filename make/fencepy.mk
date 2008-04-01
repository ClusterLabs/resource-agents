SBINDIRT=$(TARGET)

all: $(TARGET)

include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

%: $(S)/%.py
	awk "{print}(\$$1 ~ /#BEGIN_VERSION_GENERATION/){exit 0}" $^ >> $@
	echo "RELEASE_VERSION=\"${RELEASE_VERSION}\";" >> $@
	${DEF2VAR} ${SRCDIR}/config/copyright.cf sh REDHAT_COPYRIGHT >> $@
	echo "BUILD_DATE=\"(built `date`)\";" >> $@
	awk -v p=0 "(\$$1 ~ /#END_VERSION_GENERATION/){p = 1} {if(p==1)print}" $^ >> $@
	sed -i -e 's#@FENCELIBDIR@#${fencelibdir}#g' $@
ifdef MIBRESOURCE
	sed -i -e 's#@MIBDIR@#${mibdir}#g' -e 's#@SNMPBIN@#${snmpbin}#g' $@
endif
	chmod +x $@

clean: generalclean
