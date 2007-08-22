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

include ../../../make/defines.mk

TARGET= fence_scsi
SCRIPT= scsi_reserve

all: $(TARGET) $(TARGET)_test

$(TARGET): $(TARGET).pl
	: > $(TARGET)
	awk "{print}(\$$1 ~ /#BEGIN_VERSION_GENERATION/){exit 0}" $(TARGET).pl >> $(TARGET)
	echo "\$$RELEASE_VERSION=\"${RELEASE_VERSION}\";" >> $(TARGET)
	${DEF2VAR} ${BUILDDIR}/config/copyright.cf perl REDHAT_COPYRIGHT >> $(TARGET)
	echo "\$$BUILD_DATE=\"(built `date`)\";" >> $(TARGET)
	awk -v p=0 "(\$$1 ~ /#END_VERSION_GENERATION/){p = 1} {if(p==1)print}" $(TARGET).pl >> $(TARGET)
	chmod +x $(TARGET)

$(TARGET)_test: $(TARGET)_test.pl
	: > $(TARGET)_test
	awk "{print}(\$$1 ~ /#BEGIN_VERSION_GENERATION/){exit 0}" $(TARGET)_test.pl >> $(TARGET)_test
	echo "\$$RELEASE_VERSION=\"${RELEASE_VERSION}\";" >> $(TARGET)_test
	${DEF2VAR} ${BUILDDIR}/config/copyright.cf perl REDHAT_COPYRIGHT >> $(TARGET)_test
	echo "\$$BUILD_DATE=\"(built `date`)\";" >> $(TARGET)_test
	awk -v p=0 "(\$$1 ~ /#END_VERSION_GENERATION/){p = 1} {if(p==1)print}" $(TARGET)_test.pl >> $(TARGET)_test
	chmod +x $(TARGET)_test

install: all
	if [ ! -d ${sbindir} ]; then \
		install -d ${sbindir}; \
	fi
	install -m755 ${TARGET} ${TARGET}_test ${sbindir}
	if [ ! -d ${DESTDIR}/etc/init.d ]; then \
		install -d ${DESTDIR}/etc/init.d; \
	fi
	install -m755 ${SCRIPT} ${DESTDIR}/etc/init.d

uninstall:
	${UNINSTALL} ${TARGET} ${TEST_TARGET} ${sbindir}
	${UNINSTALL} ${SCRIPT} ${DESTDIR}/etc/init.d

clean:
	rm -f $(TARGET) $(TARGET)_test
