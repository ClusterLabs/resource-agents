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

SOURCE= fence_scsi.pl
TARGET= fence_scsi
SCRIPT= scsi_reserve

TEST_SOURCE= fence_scsi_test.pl
TEST_TARGET= fence_scsi_test

all: $(TARGET) $(TEST_TARGET)

fence_scsi: fence_scsi.pl
	: > $(TARGET)
	awk "{print}(\$$1 ~ /#BEGIN_VERSION_GENERATION/){exit 0}" $(SOURCE) >> $(TARGET)
	echo "\$$FENCE_RELEASE_NAME=\"${RELEASE}\";" >> $(TARGET)
	${DEF2VAR} ${BUILDDIR}/config/copyright.cf perl REDHAT_COPYRIGHT >> $(TARGET)
	echo "\$$BUILD_DATE=\"(built `date`)\";" >> $(TARGET)
	awk -v p=0 "(\$$1 ~ /#END_VERSION_GENERATION/){p = 1} {if(p==1)print}" $(SOURCE) >> $(TARGET)
	chmod +x $(TARGET)

fence_scsi_test: fence_scsi_test.pl
	: > $(TEST_TARGET)
	awk "{print}(\$$1 ~ /#BEGIN_VERSION_GENERATION/){exit 0}" $(TEST_SOURCE) >> $(TEST_TARGET)
	echo "\$$FENCE_RELEASE_NAME=\"${RELEASE}\";" >> $(TEST_TARGET)
	${DEF2VAR} ${BUILDDIR}/config/copyright.cf perl REDHAT_COPYRIGHT >> $(TEST_TARGET)
	echo "\$$BUILD_DATE=\"(built `date`)\";" >> $(TEST_TARGET)
	awk -v p=0 "(\$$1 ~ /#END_VERSION_GENERATION/){p = 1} {if(p==1)print}" $(TEST_SOURCE) >> $(TEST_TARGET)
	chmod +x $(TEST_TARGET)

install: all
	if [ ! -d ${sbindir} ]; then \
		install -d ${sbindir}; \
	fi
	install -m755 ${TARGET} ${sbindir}
	install -m755 ${TEST_TARGET} ${sbindir}
	if [ ! -d ${DESTDIR}/etc/init.d ]; then \
		install -d ${DESTDIR}/etc/init.d; \
	fi
	install -m755 ${SCRIPT} ${DESTDIR}/etc/init.d

uninstall:
	${UNINSTALL} ${TARGET} ${TEST_TARGET} ${sbindir}
	${UNINSTALL} ${SCRIPT} ${DESTDIR}/etc/init.d

clean:
	rm -f $(TARGET) $(TEST_TARGET)
