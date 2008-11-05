TARGET= gfs

INITDT=$(TARGET)

all: $(TARGET)

include ../../make/defines.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

$(TARGET): $(S)/$(TARGET).in
	cat $(S)/$(TARGET).in | sed \
		-e 's#@INITDDIR@#${initddir}#g' \
	> $(TARGET)

clean: generalclean
