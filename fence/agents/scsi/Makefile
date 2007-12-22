###############################################################################
###############################################################################
##
##  Copyright (C) 2006 Red Hat, Inc.  All rights reserved.
##  
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

TARGET= fence_scsi

SBINDIRT=$(TARGET) $(TARGET)_test
INITDT= scsi_reserve

include ../../../make/defines.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

all: $(TARGET) $(TARGET)_test

$(TARGET):
	: > $(TARGET)
	awk "{print}(\$$1 ~ /#BEGIN_VERSION_GENERATION/){exit 0}" $(S)/$(TARGET).pl >> $(TARGET)
	echo "\$$RELEASE_VERSION=\"${RELEASE_VERSION}\";" >> $(TARGET)
	${DEF2VAR} ${SRCDIR}/config/copyright.cf perl REDHAT_COPYRIGHT >> $(TARGET)
	echo "\$$BUILD_DATE=\"(built `date`)\";" >> $(TARGET)
	awk -v p=0 "(\$$1 ~ /#END_VERSION_GENERATION/){p = 1} {if(p==1)print}" $(S)/$(TARGET).pl >> $(TARGET)
	chmod +x $(TARGET)

$(TARGET)_test: 
	: > $(TARGET)_test
	awk "{print}(\$$1 ~ /#BEGIN_VERSION_GENERATION/){exit 0}" $(S)/$(TARGET)_test.pl >> $(TARGET)_test
	echo "\$$RELEASE_VERSION=\"${RELEASE_VERSION}\";" >> $(TARGET)_test
	${DEF2VAR} ${SRCDIR}/config/copyright.cf perl REDHAT_COPYRIGHT >> $(TARGET)_test
	echo "\$$BUILD_DATE=\"(built `date`)\";" >> $(TARGET)_test
	awk -v p=0 "(\$$1 ~ /#END_VERSION_GENERATION/){p = 1} {if(p==1)print}" $(S)/$(TARGET)_test.pl >> $(TARGET)_test
	chmod +x $(TARGET)_test

clean: generalclean
