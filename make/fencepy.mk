SBINDIRT=$(TARGET)

all: $(TARGET)

include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

$(TARGET):
	: > $(TARGET)
	awk "{print}(\$$1 ~ /#BEGIN_VERSION_GENERATION/){exit 0}" $(S)/$(TARGET).py >> $(TARGET)
	echo "RELEASE_VERSION=\"${RELEASE_VERSION}\";" >> $(TARGET)
	${DEF2VAR} ${SRCDIR}/config/copyright.cf sh REDHAT_COPYRIGHT >> $(TARGET)
	echo "BUILD_DATE=\"(built `date`)\";" >> $(TARGET)
	awk -v p=0 "(\$$1 ~ /#END_VERSION_GENERATION/){p = 1} {if(p==1)print}" $(S)/$(TARGET).py >> $(TARGET)
	chmod +x $(TARGET)
ifdef MIBRESOURCE
	echo ${mibdir}
	sed -i -e 's#@MIBDIR@#${mibdir}#g' -e 's#@SNMPBIN@#${snmpbin}#g' $(TARGET)
endif

clean: generalclean
