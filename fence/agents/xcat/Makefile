###############################################################################
###############################################################################
##
##  Copyright (C) Sistina Software, Inc.  1997-2002  All rights reserved.
##
###############################################################################
###############################################################################

SOURCE= fence_xcat.pl
TARGET= fence_xcat

top_srcdir=../..
include ${top_srcdir}/make/defines.mk

all: $(TARGET)

$(TARGET): $(SOURCE)
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


