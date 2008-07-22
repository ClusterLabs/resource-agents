DOCS =	gfs2.txt \
	journaling.txt \
	min-gfs.txt \
	usage.txt \
	COPYING.applications \
	COPYING.libraries \
	COPYRIGHT \
	README.licence

TARGET= cluster

LOGRORATED = $(TARGET)

all: $(TARGET)

include ../make/defines.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk
include $(OBJDIR)/make/clean.mk

$(TARGET):
	cat $(S)/$(TARGET).logrotate.in | sed \
		-e 's#@LOGDIR@#${logdir}#g' \
	> $(TARGET)

clean: generalclean
