###############################################################################
###############################################################################
##
##  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
##  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
##  
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

SOURCE= fence_bladecenter.pl
TARGET= fence_bladecenter

top_srcdir=../..
include ${top_srcdir}/make/defines.mk

all: $(TARGET)

fence_bladecenter: fence_bladecenter.pl
	: > $(TARGET)
	awk "{print}(\$$1 ~ /#BEGIN_VERSION_GENERATION/){exit 0}" $(SOURCE) >> $(TARGET)
	echo "\$$FENCE_RELEASE_NAME=\"${RELEASE}\";" >> $(TARGET)
	${top_srcdir}/scripts/define2var ${top_srcdir}/config/copyright.cf perl REDHAT_COPYRIGHT >> $(TARGET)
	echo "\$$BUILD_DATE=\"(built `date`)\";" >> $(TARGET)
	awk -v p=0 "(\$$1 ~ /#END_VERSION_GENERATION/){p = 1} {if(p==1)print}" $(SOURCE) >> $(TARGET)
	chmod +x $(TARGET)

install: all
	if [ ! -d ${sbindir} ]; then \
		install -d ${sbindir}; \
	fi
	install -m755 ${TARGET} ${sbindir}

clean:
	rm -f $(TARGET)
