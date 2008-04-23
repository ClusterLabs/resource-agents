ifndef FENCEAGENTSLIB
SBINDIRT=$(TARGET)
endif

all: $(TARGET)

include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

$(TARGET):
	${FENCEPARSE} \
		${SRCDIR}/config/copyright.cf REDHAT_COPYRIGHT \
		${RELEASE_VERSION} \
		$(S) $@ | \
	sed \
		-e 's#@FENCEAGENTSLIBDIR@#${fenceagentslibdir}#g' \
		-e 's#@MIBDIR@#${mibdir}#g' \
		-e 's#@SNMPBIN@#${snmpbin}#g' \
	> $@

	chmod +x $@

clean: generalclean
