SBINDIRT=$(TARGET)

include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

all: $(TARGET)

$(TARGET): 
	: > $(TARGET)
	awk "{print}(\$$1 ~ /#BEGIN_VERSION_GENERATION/){exit 0}" $(S)/$(TARGET).pl >> $(TARGET)
	echo "\$$RELEASE_VERSION=\"${RELEASE_VERSION}\";" >> $(TARGET)
	${DEF2VAR} ${SRCDIR}/config/copyright.cf perl REDHAT_COPYRIGHT >> $(TARGET)
	echo "\$$BUILD_DATE=\"(built `date`)\";" >> $(TARGET)
	awk -v p=0 "(\$$1 ~ /#END_VERSION_GENERATION/){p = 1} {if(p==1)print}" $(S)/$(TARGET).pl >> $(TARGET)
	chmod +x $(TARGET)

clean: generalclean
