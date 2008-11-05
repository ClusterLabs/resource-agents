TARGET1=cman
TARGET2=qdiskd

INITDT=$(TARGET1) $(TARGET2)

all: $(TARGET1) $(TARGET2)

include ../../make/defines.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

$(TARGET1): $(S)/$(TARGET1).in
	cat $(S)/$(TARGET1).in | sed \
		-e 's#@SBINDIR@#${sbindir}#g' \
		-e 's#@INITDDIR@#${initddir}#g' \
	> $(TARGET1)

$(TARGET2): $(S)/$(TARGET2).in
	cat $(S)/$(TARGET2).in | sed \
		-e 's#@SBINDIR@#${sbindir}#g' \
		-e 's#@INITDDIR@#${initddir}#g' \
	> $(TARGET2)

clean: generalclean
