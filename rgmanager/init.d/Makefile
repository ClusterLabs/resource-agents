TARGET= rgmanager

INITDT=$(TARGET)

all: $(TARGET)

include ../../make/defines.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

$(TARGET):
	cat $(S)/$(TARGET).in | sed \
		-e 's#@CONFDIR@#${CONFDIR}#g' \
		-e 's#@CONFFILE@#${CONFFILE}#g' \
	> $(TARGET)
	chmod 755 $(TARGET)

clean: generalclean

check:
