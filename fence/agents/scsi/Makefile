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

SOURCE= fence_scsi.pl
TARGET= fence_scsi

top_srcdir=../..
include ${top_srcdir}/make/defines.mk

all: $(TARGET)

fence_scsi: fence_scsi.pl
	: > $(TARGET)
	awk "{print}(\$$1 ~ /#BEGIN_VERSION_GENERATION/){exit 0}" $(SOURCE) >> $(TARGET)
	echo "\$$FENCE_RELEASE_NAME=\"${RELEASE}\";" >> $(TARGET)
	${top_srcdir}/scripts/define2var ${top_srcdir}/config/copyright.cf perl REDHAT_COPYRIGHT >> $(TARGET)
	echo "\$$BUILD_DATE=\"(built `date`)\";" >> $(TARGET)
	awk -v p=0 "(\$$1 ~ /#END_VERSION_GENERATION/){p = 1} {if(p==1)print}" $(SOURCE) >> $(TARGET)
	chmod +x $(TARGET)

copytobin: ${TARGET}
	cp ${TARGET} ${top_srcdir}/bin/${TARGET}

clean:
	rm -f $(TARGET)
